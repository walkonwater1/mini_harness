#include <stdio.h>
#include <stdbool.h>

typedef enum
{
    ENGINE_STOPPED = 0,
    ENGINE_STARTING,
    ENGINE_RUNNING
} EngineState;

static EngineState g_engineState = ENGINE_STOPPED;

static bool CheckDoorClosed(void)
{
    return true;
}

static bool CheckPowerMode(void)
{
    return true;
}

static bool CheckGearPosition(void)
{
    return true;
}

static void SendCanStartCommand(void)
{
    printf("[CAN] Send Engine Start Command\n");
}

static void SendCanStopCommand(void)
{
    printf("[CAN] Send Engine Stop Command\n");
}

static void PublishMqttReply(int result)
{
    printf("[MQTT] Reply Result=%d\n", result);
}

static bool StartEngine(void)
{
    if (!CheckDoorClosed())
    {
        return false;
    }

    if (!CheckPowerMode())
    {
        return false;
    }

    if (!CheckGearPosition())
    {
        return false;
    }

    SendCanStartCommand();

    g_engineState = ENGINE_RUNNING;

    return true;
}

static bool StopEngine(void)
{
    SendCanStopCommand();

    g_engineState = ENGINE_STOPPED;

    return true;
}

void tsp_rmt_engine_package(int cmd)
{
    bool ret = false;

    printf("[ENGINE] Receive Remote Command=%d\n",
           cmd);

    switch (cmd)
    {
        case 1:
        {
            ret = StartEngine();
            break;
        }

        case 0:
        {
            ret = StopEngine();
            break;
        }

        default:
        {
            PublishMqttReply(-1);
            return;
        }
    }

    if (ret)
    {
        PublishMqttReply(0);
    }
    else
    {
        PublishMqttReply(-2);
    }
}

int main(void)
{
    tsp_rmt_engine_package(1);

    return 0;
}
