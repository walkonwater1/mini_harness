#include <stdio.h>
#include <stdbool.h>

typedef enum
{
    DOOR_LOCKED = 0,
    DOOR_UNLOCKING,
    DOOR_UNLOCKED
} DoorState;

static DoorState g_doorState = DOOR_LOCKED;

static bool CheckVehicleSpeed(void)
{
    return true;
}

static bool CheckIgnitionOff(void)
{
    return true;
}

static bool CheckBatteryVoltage(void)
{
    return true;
}

static bool CheckAntiTheftStatus(void)
{
    return true;
}

static void SendCanLockCommand(void)
{
    printf("[CAN] Send Door Lock Command\n");
}

static void SendCanUnlockCommand(void)
{
    printf("[CAN] Send Door Unlock Command\n");
}

static void PublishMqttReply(int result)
{
    printf("[MQTT] Reply Result=%d\n", result);
}

static bool LockDoor(void)
{
    if (!CheckVehicleSpeed())
    {
        return false;
    }

    if (!CheckBatteryVoltage())
    {
        return false;
    }

    SendCanLockCommand();

    g_doorState = DOOR_LOCKED;

    return true;
}

static bool UnlockDoor(void)
{
    if (!CheckIgnitionOff())
    {
        return false;
    }

    if (!CheckAntiTheftStatus())
    {
        return false;
    }

    if (!CheckBatteryVoltage())
    {
        return false;
    }

    SendCanUnlockCommand();

    g_doorState = DOOR_UNLOCKED;

    return true;
}

void tsp_rmt_window_package(int cmd)
{
    bool ret = false;

    printf("[WINDOW] Receive Remote Command=%d\n",
           cmd);

    switch (cmd)
    {
        case 1:
        {
            ret = UnlockDoor();
            break;
        }

        case 0:
        {
            ret = LockDoor();
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
    tsp_rmt_window_package(1);

    return 0;
}
