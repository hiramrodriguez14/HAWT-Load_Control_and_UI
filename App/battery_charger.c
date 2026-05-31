#include "battery_charger.h"

#include "converter.h"

#include <stdbool.h>

#define BULK_CURRENT_A              0.20f
#define MAX_CURRENT_A               0.40f
#define ABS_VOLTAGE_V               7.30f
#define ABS_EXIT_CURRENT_A          0.04f
#define FLOAT_VOLTAGE_V             6.80f
#define RESTART_BULK_V              6.30f
#define MAX_BAT_VOLTAGE_V           7.50f
#define CURRENT_DEADBAND_A          0.10f
#define VOLTAGE_DEADBAND            0.04f

static battery_charger_state_t state;

static void update_state(float vbat, float ibat)
{
    if ((vbat > MAX_BAT_VOLTAGE_V) || (ibat > MAX_CURRENT_A)) {
        state = BATTERY_CHARGER_FAULT;
        converter_disable(CONVERTER_CHANNEL_BATTERY);
        return;
    }

    switch (state) {
        case BATTERY_CHARGER_BULK:
            if (vbat >= ABS_VOLTAGE_V) {
                state = BATTERY_CHARGER_ABSORPTION;
            }
            break;

        case BATTERY_CHARGER_ABSORPTION:
            if ((ibat <= ABS_EXIT_CURRENT_A) && (vbat >= ABS_VOLTAGE_V)) {
                state = BATTERY_CHARGER_FLOAT;
            }
            break;

        case BATTERY_CHARGER_FLOAT:
            if (vbat <= RESTART_BULK_V) {
                state = BATTERY_CHARGER_BULK;
            }
            break;

        case BATTERY_CHARGER_FAULT:
            converter_disable(CONVERTER_CHANNEL_BATTERY);
            if ((vbat < ABS_VOLTAGE_V) && (ibat < BULK_CURRENT_A)) {
                state = BATTERY_CHARGER_BULK;
            }
            break;
    }
}

void battery_charger_init(void)
{
    state = BATTERY_CHARGER_BULK;
}

void battery_charger_update(float vbat, float ibat)
{
    float vref = ABS_VOLTAGE_V;

    update_state(vbat, ibat);

    switch (state) {
        case BATTERY_CHARGER_BULK:
            vref = ABS_VOLTAGE_V;
            if (ibat < BULK_CURRENT_A) {
                converter_increase_output(CONVERTER_CHANNEL_BATTERY);
            } else if (ibat > (BULK_CURRENT_A + CURRENT_DEADBAND_A)) {
                converter_decrease_output(CONVERTER_CHANNEL_BATTERY);
            }
            break;

        case BATTERY_CHARGER_ABSORPTION:
            vref = ABS_VOLTAGE_V;
            if (vbat < (vref - VOLTAGE_DEADBAND)) {
                converter_increase_output(CONVERTER_CHANNEL_BATTERY);
            } else if (vbat > (vref + VOLTAGE_DEADBAND)) {
                converter_decrease_output(CONVERTER_CHANNEL_BATTERY);
            }
            break;

        case BATTERY_CHARGER_FLOAT:
            vref = FLOAT_VOLTAGE_V;
            if (vbat < (vref - VOLTAGE_DEADBAND)) {
                converter_increase_output(CONVERTER_CHANNEL_BATTERY);
            } else if (vbat > (vref + VOLTAGE_DEADBAND)) {
                converter_decrease_output(CONVERTER_CHANNEL_BATTERY);
            }
            break;

        case BATTERY_CHARGER_FAULT:
            converter_disable(CONVERTER_CHANNEL_BATTERY);
            break;
    }

    converter_set_voltage_reference(CONVERTER_CHANNEL_BATTERY, vref);
    converter_apply(CONVERTER_CHANNEL_BATTERY, state == BATTERY_CHARGER_FAULT);
}

battery_charger_state_t battery_charger_get_state(void)
{
    return state;
}

const char *battery_charger_state_to_string(battery_charger_state_t charger_state)
{
    switch (charger_state) {
        case BATTERY_CHARGER_BULK:
            return "BULK";

        case BATTERY_CHARGER_ABSORPTION:
            return "ABSORPTION";

        case BATTERY_CHARGER_FLOAT:
            return "FLOAT";

        case BATTERY_CHARGER_FAULT:
            return "FAULT";

        default:
            return "UNKNOWN";
    }
}
