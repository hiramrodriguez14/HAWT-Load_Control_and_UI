#include "dump_load.h"

#include "ti_msp_dl_config.h"

static dump_load_state_t state;

void dump_load_init(void)
{
    dump_load_disable();
}

void dump_load_enable(void)
{
#if defined(DUMP_LOAD_RELAY_PORT) && defined(DUMP_LOAD_RELAY_IN1_PIN)
    DL_GPIO_setPins(DUMP_LOAD_RELAY_PORT, DUMP_LOAD_RELAY_IN1_PIN);
#endif
    state = DUMP_LOAD_ENABLED;
}

void dump_load_disable(void)
{
#if defined(DUMP_LOAD_RELAY_PORT) && defined(DUMP_LOAD_RELAY_IN1_PIN)
    DL_GPIO_clearPins(DUMP_LOAD_RELAY_PORT, DUMP_LOAD_RELAY_IN1_PIN);
#endif
    state = DUMP_LOAD_DISABLED;
}

dump_load_state_t dump_load_get_state(void)
{
    return state;
}

const char *dump_load_state_to_string(dump_load_state_t dump_load_state)
{
    return (dump_load_state == DUMP_LOAD_ENABLED) ? "ON" : "OFF";
}
