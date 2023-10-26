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

#define MAX_MESSAGE_SIZE 32

static bool isInit = false;
uint32_t loggedTimestamp = 0;

void jevoisTimestamp()
{
    char buf[MAX_MESSAGE_SIZE];
    char c = '0';

    for (int i = 0; i < MAX_MESSAGE_SIZE; i++)
    {
        if (!uart2GetCharWithDefaultTimeout(&c)) break;

        if (c == '\n')
        {
            buf[i] = '\0';
            loggedTimestamp = atol(buf);
            DEBUG_PRINT("%lu\n", loggedTimestamp);
            break;
        }

        buf[i] = c;
    }
}

void jevoisTask(void *param)
{
    systemWaitStart();

    while (true)
    {
        jevoisTimestamp();
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
LOG_ADD(LOG_UINT32, timestamp, &loggedTimestamp)
LOG_GROUP_STOP(jevois)
