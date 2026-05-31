#include "mppt.h"

#include "converter.h"

#define MPPT_DUTY_MIN       0.05f
#define MPPT_DUTY_MAX       0.99f
#define MPPT_DUTY_STEP      0.0005f
#define MPPT_MIN_INPUT_V    5.0f

static mppt_state_t state;
static float target_duty;
static float previous_input_power;
static float previous_input_voltage;
static int direction;
static unsigned char enabled;

void mppt_init(void)
{
    state = MPPT_STATE_IDLE;
    target_duty = 0.30f;
    previous_input_power = 0.0f;
    previous_input_voltage = 0.0f;
    direction = 1;
    enabled = 0U;
}

void mppt_enable(void)
{
    enabled = 1U;
    state = MPPT_STATE_SEARCH;
    target_duty = converter_get_duty(CONVERTER_CHANNEL_MPPT);
    previous_input_power = 0.0f;
    previous_input_voltage = 0.0f;
    direction = 1;
}

void mppt_disable(void)
{
    enabled = 0U;
    state = MPPT_STATE_IDLE;
    converter_disable(CONVERTER_CHANNEL_MPPT);
}

void mppt_update(float vin, float iin, float vout, float iout)
{
    float input_power = vin * iin;

    (void)vout;
    (void)iout;

    if (!enabled) {
        state = MPPT_STATE_IDLE;
        return;
    }

    if (vin < MPPT_MIN_INPUT_V) {
        state = MPPT_STATE_FAULT;
        converter_disable(CONVERTER_CHANNEL_MPPT);
        return;
    }

    if (state == MPPT_STATE_FAULT) {
        state = MPPT_STATE_SEARCH;
    }

    if (input_power < previous_input_power) {
        direction = -direction;
    }

    if (direction > 0) {
        converter_increase_output(CONVERTER_CHANNEL_MPPT);
    } else {
        converter_decrease_output(CONVERTER_CHANNEL_MPPT);
    }

    target_duty += (float)direction * MPPT_DUTY_STEP;

    if (target_duty > MPPT_DUTY_MAX) {
        target_duty = MPPT_DUTY_MAX;
        state = MPPT_STATE_LIMITED;
    } else if (target_duty < MPPT_DUTY_MIN) {
        target_duty = MPPT_DUTY_MIN;
        state = MPPT_STATE_LIMITED;
    } else {
        state = (previous_input_voltage == 0.0f) ? MPPT_STATE_SEARCH : MPPT_STATE_TRACK;
    }

    previous_input_power = input_power;
    previous_input_voltage = vin;

    converter_apply(CONVERTER_CHANNEL_MPPT, state == MPPT_STATE_FAULT);
}

mppt_state_t mppt_get_state(void)
{
    return state;
}

float mppt_get_target_duty(void)
{
    return target_duty;
}

const char *mppt_state_to_string(mppt_state_t mppt_state)
{
    switch (mppt_state) {
        case MPPT_STATE_IDLE:
            return "IDLE";

        case MPPT_STATE_SEARCH:
            return "SEARCH";

        case MPPT_STATE_TRACK:
            return "TRACK";

        case MPPT_STATE_LIMITED:
            return "LIMITED";

        case MPPT_STATE_FAULT:
            return "FAULT";

        default:
            return "UNKNOWN";
    }
}
