/**
 * ,---------,       ____  _ __
 * |  ,-^-,  |      / __ )(_) /_______________ _____  ___
 * | (  O  ) |     / __  / / __/ ___/ ___/ __ `/_  / / _ \
 * | / ,--´  |    / /_/ / / /_/ /__/ /  / /_/ / / /_/  __/
 *    +------`   /_____/_/\__/\___/_/   \__,_/ /___/\___/
 *
 * Crazyflie control firmware
 *
 * Copyright (C) 2019 Bitcraze AB
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, in version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * estimator_complementary.c - a complementary estimator
 */

#include "stm32f4xx.h"

#include "FreeRTOS.h"
#include "queue.h"

#include "stabilizer.h"
#include "estimator_complementary.h"
#include "sensfusion6.h"
#include "position_estimator.h"
#include "velocity_estimator.h"
#include "sensors.h"
#include "stabilizer_types.h"
#include "static_mem.h"
#include "physicalConstants.h"
#include "filter.h"
#include "cf_math.h"

#include "log.h"
#include "param.h"

#define DEBUG_MODULE "ESTCOMP"
#include "debug.h"

static bool resetEstimation = false;
static Axis3f gyro;
static Axis3f acc;
static baro_t baro;
static tofMeasurement_t tof;

static Axis3f gyro_accum = {.axis={0}};;
static Axis3f acc_accum = {.axis={0}};
static float baro_accum = 0.0f;

static uint8_t gyro_count = 0;
static uint8_t acc_count = 0;
static uint8_t baro_count = 0;


static Axis3f positionPrediction;
static bool isFlying = false;
static uint8_t flightCounter = 0;
static int thrustID;
static float baro_ground_level;
static bool baro_ground_set = false;

static state_t estimator_state;

//#define SWARMING_USE_OPTITRACK

#define ATTITUDE_UPDATE_RATE RATE_250_HZ
#define ATTITUDE_UPDATE_DT 1.0f/ATTITUDE_UPDATE_RATE

#define POS_UPDATE_RATE RATE_100_HZ
#define POS_UPDATE_DT 1.0f/POS_UPDATE_RATE

#define LOW_PASS_FILTER_CUTOFF_FREQ_HZ 5
#define LOW_PASS_FILTER_TAU (1.0/(2*3.1415*LOW_PASS_FILTER_CUTOFF_FREQ_HZ))
#define DEG2RAD (3.1415f/180)

static Axis3f drag_coef;

positionMeasurement_t ext_pos;
bool use_ext_pos = false;

static Butterworth2LowPass gz_filter;
static float filtered_gz = 0.0f;

static Axis3f horizontal_acc;

struct CompFilterParams_s {
  float k1;
  float k2;
};

static struct CompFilterParams_s cf_vx_params = {.k1=20.0f, .k2=1.0f};
static struct CompFilterParams_s cf_vy_params = {.k1=13.0f, .k2=0.5f};
static struct CompFilterParams_s cf_vz_params = {.k1=1.3f, .k2=0.07};
static struct CompFilterParams_s cf_alt_params = {.k1=23.0f, .k2=0.4f};

static ComplementaryFilter2_t vx_comp_filter;
static ComplementaryFilter2_t vy_comp_filter;
static ComplementaryFilter2_t vz_comp_filter;
static ComplementaryFilter2_t alt_comp_filter;
static state_t test_states;


void estimatorComplementaryInit(void)
{
  sensfusion6Init();
  
  init_butterworth_2_low_pass(&gz_filter, LOW_PASS_FILTER_TAU, ATTITUDE_UPDATE_DT, 0);

  init_complementary_filter_vxy(&vx_comp_filter, cf_vx_params.k1, cf_vx_params.k2, POS_UPDATE_DT);
  init_complementary_filter_vxy(&vy_comp_filter, cf_vy_params.k1, cf_vy_params.k2, POS_UPDATE_DT);
  init_complementary_filter_vz(&vz_comp_filter, cf_vz_params.k1, cf_vz_params.k2, POS_UPDATE_DT);
  init_complementary_filter_z(&alt_comp_filter, cf_alt_params.k2, cf_alt_params.k2, POS_UPDATE_DT);

  drag_coef.x = 4.2;
  drag_coef.y = 1.8;
  positionPrediction.x = 0.0;
  positionPrediction.y = 0.0;
  positionPrediction.z = 0.0;
  isFlying = false;
  baro_ground_set = false;
  thrustID = logGetVarId("stabilizer", "thrust");
}

bool estimatorComplementaryTest(void)
{
  bool pass = true;

  pass &= sensfusion6Test();

  return pass;
}

void estimatorComplementary(state_t *state, const uint32_t tick)
{
  if (resetEstimation){
    estimatorComplementaryInit();
    resetEstimation = false;
  }

  use_ext_pos = false;
  
  // Pull the latest sensors values of interest; discard the rest
  measurement_t m;
  while (estimatorDequeue(&m)) {
    switch (m.type)
    {
    case MeasurementTypeGyroscope:
      // gyro = m.data.gyroscope.gyro;
      gyro_accum.x += m.data.gyroscope.gyro.x;
      gyro_accum.y += m.data.gyroscope.gyro.y;
      gyro_accum.z += m.data.gyroscope.gyro.z;
      gyro_count++;
      break;
    case MeasurementTypeAcceleration:
      // acc = m.data.acceleration.acc;
      acc_accum.x += m.data.acceleration.acc.x;
      acc_accum.y += m.data.acceleration.acc.y;
      acc_accum.z += m.data.acceleration.acc.z;
      acc_count++;
      break;
    case MeasurementTypeBarometer:
      baro_accum += m.data.barometer.baro.asl;
      baro_count += 1;
      break;
    case MeasurementTypeTOF:
      tof = m.data.tof;
      break;
    case MeasurementTypePosition:
      ext_pos = m.data.position;
      use_ext_pos = true;
      break;
    case MeasurementTypePose:
      ext_pos.x = m.data.pose.x;
      ext_pos.y = m.data.pose.y;
      ext_pos.z = m.data.pose.z;
      use_ext_pos = true;
      // don't update attitude as optitrack attitude is super noisy for flapper
      break;
    default:
      break;
    }
  }

  // Update filter
  if (RATE_DO_EXECUTE(ATTITUDE_UPDATE_RATE, tick)) {
    if (gyro_count){
      gyro.x = gyro_accum.x/gyro_count;
      gyro.y = gyro_accum.y/gyro_count;
      gyro.z = gyro_accum.z/gyro_count;
      gyro_accum = (Axis3f){.axis={0}};
      gyro_count = 0;
    }

    if (acc_count){
      acc.x = acc_accum.x/acc_count;
      acc.y = acc_accum.y/acc_count;
      acc.z = acc_accum.z/acc_count;
      acc_accum = (Axis3f){.axis={0}};
      acc_count = 0;
    }
    
    sensfusion6UpdateQ(gyro.x, gyro.y, gyro.z,
                        acc.x, acc.y, acc.z,
                        ATTITUDE_UPDATE_DT);

    // Save attitude, adjusted for the legacy CF2 body coordinate system
    sensfusion6GetEulerRPY(&state->attitude.roll, &state->attitude.pitch, &state->attitude.yaw);

    // Save quaternion, hopefully one day this could be used in a better controller.
    // Note that this is not adjusted for the legacy coordinate system
    sensfusion6GetQuaternion(
      &state->attitudeQuaternion.x,
      &state->attitudeQuaternion.y,
      &state->attitudeQuaternion.z,
      &state->attitudeQuaternion.w);

    state->acc.z = sensfusion6GetAccZWithoutGravity(acc.x,
                                                    acc.y,
                                                    acc.z);

    positionUpdateVelocity(state->acc.z, ATTITUDE_UPDATE_DT);

    float ctheta = cos(-state->attitude.pitch * DEG2RAD);
    float sphi = sin(state->attitude.roll * DEG2RAD);
    float cphi = cos(state->attitude.roll * DEG2RAD);
    float tmp_gz = (gyro.y * sphi/ctheta + gyro.z * cphi/ctheta) * DEG2RAD;
    // float tmp_gz = gyro.z * DEG2RAD;
    filtered_gz = update_butterworth_2_low_pass(&gz_filter, tmp_gz);
  }
  if (RATE_DO_EXECUTE(POS_UPDATE_RATE, tick)) {    
    if (!isFlying && logGetFloat(thrustID) > 35000){
      isFlying = true;
    }

    if (isFlying && logGetFloat(thrustID) <= 1000) {
      flightCounter ++;
      if (flightCounter > 20){
        isFlying = false;
      }
    }

    // average past baro asl measurements and put ground level to 0
    if (baro_count > 0){
      baro.asl = (baro_accum/baro_count);
      baro_accum = 0;
      baro_count = 0;
      
      // callibrate baro on first iteration
      if (!baro_ground_set){
        DEBUG_PRINT("Reset Baro ground level");
        baro_ground_level = baro.asl;
        baro_ground_set = true;
      }
      baro.asl = baro.asl - baro_ground_level;
    }

    positionEstimate(state, &baro, &tof, POS_UPDATE_DT, tick);

    float tmp;
    float cphi = cos(state->attitude.roll*DEG2RAD);
    float sphi = sin(state->attitude.roll*DEG2RAD);
    float ctheta = cos(-state->attitude.pitch*DEG2RAD);
    float stheta = sin(-state->attitude.pitch*DEG2RAD);

    // Dynamic velocity model (linear drag)
    tmp = GRAVITY_MAGNITUDE*stheta/ctheta - drag_coef.x*ctheta*ctheta*state->velocity.x;
    state->velocity.x += POS_UPDATE_DT*tmp;
    tmp = -GRAVITY_MAGNITUDE*sphi/ctheta/cphi - drag_coef.y*cphi*cphi*state->velocity.y;
    state->velocity.y += POS_UPDATE_DT*tmp;

    // Complementary filters with rotated (horizontal) acceleration
    test_states.velocity.x = state->velocity.x;
    test_states.velocity.y = state->velocity.y;
    horizontal_acc.x = (ctheta*acc.x +stheta*sphi*acc.y + stheta*cphi*acc.z) * GRAVITY_MAGNITUDE;
    horizontal_acc.y = (cphi*acc.y - sphi*acc.z) * GRAVITY_MAGNITUDE;
    horizontal_acc.z = (-stheta*acc.x + ctheta*sphi*acc.y + cphi*ctheta*acc.z) * GRAVITY_MAGNITUDE;
    // test_states.velocity.x = update_complementary_filter(&vx_comp_filter, horizontal_acc.x, state->velocity.x);
    // test_states.velocity.y = update_complementary_filter(&vy_comp_filter, horizontal_acc.y, state->velocity.y);
    test_states.position.z = update_complementary_filter(&alt_comp_filter, horizontal_acc.z - GRAVITY_MAGNITUDE, baro.asl);
    state->velocity.z = update_complementary_filter(&vz_comp_filter, horizontal_acc.z - GRAVITY_MAGNITUDE, baro.asl);

    if (isFlying){
      state->position.x += POS_UPDATE_DT * state->velocity.x;
      state->position.y += POS_UPDATE_DT * state->velocity.y;
      positionPrediction.x += POS_UPDATE_DT * state->velocity.x;
      positionPrediction.y += POS_UPDATE_DT * state->velocity.y;
      positionPrediction.z = state->position.z;
    }
  }

  // set variables for swarming state
  estimator_state.velocity.x = state->velocity.x;
  estimator_state.velocity.y = state->velocity.y;
  estimator_state.velocity.z = state->velocity.z;
  estimator_state.position.z = state->position.z;
  estimator_state.attitude.roll = state->attitude.roll;
  estimator_state.attitude.pitch = state->attitude.pitch;
  estimator_state.attitude.yaw = state->attitude.yaw;

}

void complementaryGetSwarmInfo(float* vx, float* vy, float* vz, float* gyroZ, float* posZ){
  *vx = estimator_state.velocity.x;
  *vy= estimator_state.velocity.y;
  *vz = estimator_state.velocity.z;
  *gyroZ = filtered_gz;
  // *gyroZ = 0.0f;
  // float stheta = sin(-estimator_state.attitude.pitch * DEG2RAD);
  // float ctheta = cos(-estimator_state.attitude.pitch * DEG2RAD);
  // float cphi = cos(estimator_state.attitude.roll * DEG2RAD);
  // *gyroZ = (-gyro.x * stheta/cphi + gyro.z * ctheta/cphi) * DEG2RAD;
  *posZ = estimator_state.position.z;
}

LOG_GROUP_START(flapperModel)
  LOG_ADD(LOG_FLOAT, posX, &positionPrediction.x)
  LOG_ADD(LOG_FLOAT, posY, &positionPrediction.y)
  LOG_ADD(LOG_FLOAT, posZ, &positionPrediction.z)
  LOG_ADD(LOG_FLOAT, Altitude, &test_states.position.z)
  LOG_ADD(LOG_FLOAT, velX, &test_states.velocity.x)
  LOG_ADD(LOG_FLOAT, velY, &test_states.velocity.y)
  LOG_ADD(LOG_FLOAT, velZ, &test_states.velocity.z)
  LOG_ADD(LOG_UINT8, flying, &isFlying)
LOG_GROUP_STOP(flapperModel)

LOG_GROUP_START(compInputs)
  LOG_ADD(LOG_FLOAT, velX, &test_states.velocity.x)
  LOG_ADD(LOG_FLOAT, velY, &test_states.velocity.y)
  LOG_ADD(LOG_FLOAT, baroAG, &baro.asl)
  LOG_ADD(LOG_FLOAT, haccX, &horizontal_acc.x)
  LOG_ADD(LOG_FLOAT, haccY, &horizontal_acc.y)
  LOG_ADD(LOG_FLOAT, haccZ, &horizontal_acc.z)
  LOG_ADD(LOG_FLOAT, accX, &acc.x)
  LOG_ADD(LOG_FLOAT, accY, &acc.y)
  LOG_ADD(LOG_FLOAT, accZ, &acc.z)
LOG_GROUP_STOP(compInputs)

PARAM_GROUP_START(complementaryFilter)
  PARAM_ADD(PARAM_UINT8, reset, &resetEstimation)
  PARAM_ADD(PARAM_FLOAT, dragX, &drag_coef.x)
  PARAM_ADD(PARAM_FLOAT, dragY, &drag_coef.y)
  PARAM_ADD(PARAM_FLOAT, dragZ, &drag_coef.z)
  //PARAM_ADD(PARAM_FLOAT, cT, &thrust_coef)
PARAM_GROUP_STOP(complementaryFilter)

// PARAM_GROUP_START(velocityCFilter)
//   PARAM_ADD(PARAM_FLOAT, vxK1, &cf_vx_params.k1)
//   PARAM_ADD(PARAM_FLOAT, vxK2, &cf_vx_params.k2)
//   PARAM_ADD(PARAM_FLOAT, vyK1, &cf_vy_params.k1)
//   PARAM_ADD(PARAM_FLOAT, vyK2, &cf_vy_params.k2)
//   PARAM_ADD(PARAM_FLOAT, vzK1, &cf_vz_params.k1)
//   PARAM_ADD(PARAM_FLOAT, vzK2, &cf_vz_params.k2)
//   PARAM_ADD(PARAM_FLOAT, altK1, &cf_alt_params.k1)
//   PARAM_ADD(PARAM_FLOAT, altK2, &cf_alt_params.k2)
// PARAM_GROUP_STOP(velocityCFilter)
