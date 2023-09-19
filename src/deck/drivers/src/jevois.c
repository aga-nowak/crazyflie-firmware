#define DEBUG_MODULE "JEVOIS"

#include <stddef.h>
#include <stdlib.h>

#include "debug.h"
#include "log.h"
#include "deck.h"
#include "uart2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "system.h"

#define MAX_MESSAGE_SIZE 32 // max no of chars in the message

static bool isInit = false;

int16_t loggedErrorX = 0;
int16_t loggedErrorY = 0;
int16_t loggedWidth = 0;
int16_t loggedHeight = 0;

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

void jevoisTask(void *param)
{
    systemWaitStart();

    while (true)
    {
        snakeGate();
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
LOG_GROUP_STOP(jevois)
