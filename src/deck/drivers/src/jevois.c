#define DEBUG_MODULE "JEVOIS"

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#include "debug.h"
#include "log.h"
#include "param.h"
#include "deck.h"
#include "uart2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "system.h"
#include "eventtrigger.h"
#include "commander.h"

#define MAX_MESSAGE_SIZE 32 // max no of chars in the message

#define DEFAULT_HEIGHT 1.0f
#define YAW_BIAS 8.0f;

#define ALPHA_YAW 0.8f
#define ALPHA_Z 0.8f

#define ERROR_GAIN_Y 3.0f
#define ERROR_GAIN_X 0.001f

#define TIME_LIMIT 60.0f

static bool isInit = false;

int16_t loggedErrorX = 0;
int16_t loggedErrorY = 0;
int16_t loggedWidth = 0;
int16_t loggedHeight = 0;

uint8_t start_flight = false;

static float current_yawrate = 0.0f;
static float current_z_cmd = DEFAULT_HEIGHT;
static setpoint_t current_setpoint;

void snakeGate()
{
    char c = '0';
    char errorX[MAX_MESSAGE_SIZE];
    char errorY[MAX_MESSAGE_SIZE];
    char width[MAX_MESSAGE_SIZE];
    char height[MAX_MESSAGE_SIZE];

    while (c != 'x') uart2Getchar(&c);

    for (int i = 0; i < MAX_MESSAGE_SIZE; i++)
    {
        uart2Getchar(&c);
        if (c == 'y')
        {
            errorX[i] = '\0';
            break;
        }
        errorX[i] = c;
    }

    for (int i = 0; i < MAX_MESSAGE_SIZE; i++)
    {
        uart2Getchar(&c);
        if (c == 'w')
        {
            errorY[i] = '\0';
            break;
        }
        errorY[i] = c;
    }

    for (int i = 0; i < MAX_MESSAGE_SIZE; i++)
    {
        uart2Getchar(&c);
        if (c == 'h')
        {
            width[i] = '\0';
            break;
        }
        width[i] = c;
    }

    for (int i = 0; i < MAX_MESSAGE_SIZE; i++)
    {
        uart2Getchar(&c);
        if (c == 'e')
        {
            height[i] = '\0';
            break;
        }
        height[i] = c;
    }

    loggedErrorX = atoi(errorX);
    loggedErrorY = atoi(errorY);
    loggedWidth = atoi(width);
    loggedHeight = atoi(height);
}


void jevoisInitCommand(){
    current_setpoint.mode.roll = modeAbs;
    current_setpoint.mode.pitch = modeAbs;
    current_setpoint.mode.yaw = modeVelocity;

    current_setpoint.mode.z = modeAbs;
}

void jevoisSendTakeOffCommand(){
    current_setpoint.attitude.roll = 0.0f;
    current_setpoint.attitude.pitch = 0.0;
    current_setpoint.attitudeRate.yaw = 0.0f;
    current_setpoint.position.z = DEFAULT_HEIGHT;

    commanderSetSetpoint(&current_setpoint, COMMANDER_PRIORITY_CRTP);
}

void jevoisSendForwardCommand(){
    current_setpoint.attitude.roll = 0.0f;
    current_setpoint.attitude.pitch = -10.0f;
    current_setpoint.attitudeRate.yaw = 0.0f;

    current_setpoint.position.z = current_z_cmd;

    commanderSetSetpoint(&current_setpoint, COMMANDER_PRIORITY_CRTP);

}

void jevoisSendLandingCommand(){
    current_setpoint.attitude.roll = 0.0f;
    current_setpoint.attitude.pitch = 0.0f;
    current_setpoint.attitudeRate.yaw = 0.0f;

    current_setpoint.position.z = 0.0f;

    commanderSetSetpoint(&current_setpoint, COMMANDER_PRIORITY_CRTP);

}

void jevoisSendGateCommand(int errorX, int errorY){
    logVarId_t varZid = logGetVarId("stateEstimate", "z");
    float z = logGetFloat(varZid);

    float new_yaw_rate = ALPHA_YAW * current_yawrate - (1-ALPHA_YAW)*ERROR_GAIN_Y*errorY + YAW_BIAS;
    float new_z_cmd = ALPHA_Z * current_z_cmd + (1-ALPHA_Z)* (ERROR_GAIN_X*errorX + z);

    if (new_z_cmd > 1.8f){
        new_z_cmd = 1.8f;
    }

    current_setpoint.attitude.roll = 0.0f;
    current_setpoint.attitude.pitch = -5.0;
    current_setpoint.attitudeRate.yaw = new_yaw_rate;
    current_setpoint.position.z = new_z_cmd;

    commanderSetSetpoint(&current_setpoint, COMMANDER_PRIORITY_CRTP);

    current_yawrate = new_yaw_rate;
    current_z_cmd = new_z_cmd;
}


void jevoisTask(void *param)
{
    systemWaitStart();
    jevoisInitCommand();

    float errorX = 0.0f;
    float errorY = 0.0f; 
    float width = 0.0f; 
    float height = 0.0f;

    while (!start_flight)
    {
        vTaskDelay(M2T(100));
    }
    

    float time_passed = 0.0f;
    while (time_passed < 5){
        if (!start_flight){jevoisSendLandingCommand();}
        jevoisSendTakeOffCommand();
        vTaskDelay(M2T(50));
        time_passed += 0.05f;
    }

    time_passed = 0.0f;
    while ((time_passed < TIME_LIMIT) && (width<180) && (height<180))
    {
        if (!start_flight){jevoisSendLandingCommand();}

        snakeGate();

        errorX = loggedErrorX;
        errorY = loggedErrorY;
        width = loggedWidth;
        height = loggedHeight;
        
        jevoisSendGateCommand(errorX, errorY);
        
        vTaskDelay(M2T(50));
        time_passed += 0.05f;
    }

    time_passed = 0.0f;
    while (time_passed < 5){
        if (!start_flight){jevoisSendLandingCommand();}

        jevoisSendForwardCommand();
                
        vTaskDelay(M2T(50));
        time_passed += 0.05f;
    }

    time_passed = 0.0f;
    logVarId_t varZid = logGetVarId("stateEstimate", "z");
    float z = logGetFloat(varZid);
    while ((time_passed < 10) && (z > 0.1f)){

        jevoisSendLandingCommand();
        z = logGetFloat(varZid);
                
        vTaskDelay(M2T(50));
        time_passed += 0.05f;
    }

    while (true)
    {
        vTaskDelay(M2T(50));
    }
    
}

static void jevoisInit()
{
    DEBUG_PRINT("Initialize driver\n");

    uart2Init(115200);
    
    xTaskCreate(jevoisTask, JEVOIS_TASK_NAME, JEVOIS_TASK_STACKSIZE, NULL, JEVOIS_TASK_PRI, NULL);
    
    isInit = true;
}

static bool jevoisTest()
{
    return isInit;
}

static const DeckDriver jevoisDriver = {
  .name = "jevois",
  .init = jevoisInit,
  .test = jevoisTest,
};

DECK_DRIVER(jevoisDriver);


/**
 * Logging variables for the jevois camera
 */
LOG_GROUP_START(jevois)
LOG_ADD(LOG_INT16, errorx, &loggedErrorX)
LOG_ADD(LOG_INT16, errory, &loggedErrorY)
LOG_ADD(LOG_INT16, width, &loggedWidth)
LOG_ADD(LOG_INT16, height, &loggedHeight)
LOG_ADD(LOG_FLOAT, yawCmd, &current_yawrate)
LOG_ADD(LOG_FLOAT, zCmd, &current_z_cmd)
LOG_GROUP_STOP(jevois)

PARAM_GROUP_START(jevois)
PARAM_ADD(PARAM_UINT8, startFlight, &start_flight)
PARAM_GROUP_STOP(jevois)