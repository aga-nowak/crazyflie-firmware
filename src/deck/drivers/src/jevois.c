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

#define MAX_MESSAGE_SIZE 16 // max no of chars in the message

// EVENTTRIGGER(jevoisErrorLog, float, errorx, float, errory)

static bool isInit = false;

float loggedErrorX = 0;
float loggedErrorY = 0;

void jevoisError()
{
    char errorX[MAX_MESSAGE_SIZE];
    char errorY[MAX_MESSAGE_SIZE];

    char c = '0';

    for (int i = 0; i < MAX_MESSAGE_SIZE; i++)
    {
        uart2Getchar(&c);

        if (c == '-') break;
        
        errorX[i] = c;
    }

    loggedErrorX = atof(errorX);

    for (int i = 0; i < MAX_MESSAGE_SIZE; i++)
    {
        uart2Getchar(&c);

        if (c == '\n') break;
        
        errorY[i] = c;
    }

    loggedErrorY = atof(errorY);

    // eventTrigger(&eventTrigger_jevoisErrorLog);
}

void jevoisTask(void *param)
{
    systemWaitStart();

    while (true)
    {
        jevoisError();
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
LOG_ADD(LOG_FLOAT, errorx, &loggedErrorX)
LOG_ADD(LOG_FLOAT, errory, &loggedErrorY)
LOG_GROUP_STOP(jevois)
