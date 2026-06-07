#include "load_relay.h"

#include "ti_msp_dl_config.h"

#define LOAD_DISCONNECT_VOLTAGE_V 5.85f
#define LOAD_RECONNECT_VOLTAGE_V  6.45f

static load_relay_state_t state;

void load_relay_init(void)
{
    load_relay_disable();
}

void load_relay_enable(void)
{
    DL_GPIO_clearPins(BATTERY_RELAY_PORT, BATTERY_RELAY_IN2_PIN);
    state = LOAD_RELAY_ENABLED;
}

void load_relay_disable(void)
{
    DL_GPIO_setPins(BATTERY_RELAY_PORT, BATTERY_RELAY_IN2_PIN);
    state = LOAD_RELAY_DISABLED;
}

void load_relay_update(float vbat)
{
    if (vbat <= LOAD_DISCONNECT_VOLTAGE_V) {
        load_relay_disable();
    } else if (vbat >= LOAD_RECONNECT_VOLTAGE_V) {
        load_relay_enable();
    }
}

load_relay_state_t load_relay_get_state(void)
{
    return state;
}

const char *load_relay_state_to_string(load_relay_state_t relay_state)
{
    return (relay_state == LOAD_RELAY_ENABLED) ? "ON" : "OFF";
}
