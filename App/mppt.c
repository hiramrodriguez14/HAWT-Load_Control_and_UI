#include "mppt.h"

#include "converter.h"
#include "drivers/uart_debug.h"

#define MPPT_DUTY_MIN       0.05f
#define MPPT_DUTY_MAX       0.99f
#define MPPT_DUTY_STEP      (4.0f / 255.0f)
#define MPPT_MIN_INPUT_V    5.0f
#define MPPT_START_DUTY     0.30f
#define MPPT_POWER_DEADBAND_W 0.10f

static mppt_state_t state;
static float target_duty;
static float previous_input_power;
static int direction;
static unsigned char enabled;
static unsigned char have_previous_sample;

static void perturb_output(void)
{
    target_duty += (float)direction * MPPT_DUTY_STEP;

    if (target_duty > MPPT_DUTY_MAX) {
        target_duty = MPPT_DUTY_MAX;
        state = MPPT_STATE_LIMITED;
    } else if (target_duty < MPPT_DUTY_MIN) {
        target_duty = MPPT_DUTY_MIN;
        state = MPPT_STATE_LIMITED;
    } else {
        state = MPPT_STATE_TRACK;
    }

    converter_set_output(CONVERTER_CHANNEL_MPPT, target_duty);
}

void mppt_init(void)
{
    state = MPPT_STATE_IDLE;
    target_duty = 0.30f;
    previous_input_power = 0.0f;
    direction = 1;
    enabled = 0U;
    have_previous_sample = 0U;
}

void mppt_enable(void)
{
    enabled = 1U;
    state = MPPT_STATE_SEARCH;
    target_duty = MPPT_START_DUTY;
    converter_set_output(CONVERTER_CHANNEL_MPPT, target_duty);
    previous_input_power = 0.0f;
    direction = 1;
    have_previous_sample = 0U;
}

void mppt_disable(void)
{
    enabled = 0U;
    state = MPPT_STATE_IDLE;
    have_previous_sample = 0U;
    converter_disable(CONVERTER_CHANNEL_MPPT);
}

void mppt_update(float vin, float iin, float vout, float iout)
{
    float input_power = vin * iin;
    float delta_power;

    (void)vout;
    (void)iout;

    if (!enabled) {
        state = MPPT_STATE_IDLE;
        return;
    }

    if (vin < MPPT_MIN_INPUT_V) {
        state = MPPT_STATE_FAULT;
        have_previous_sample = 0U;
        converter_disable(CONVERTER_CHANNEL_MPPT);
        uart_printf("MPPTDBG state=FAULT vin=%.3f iin=%.3f pin=%.3f pot=%u duty=%.3f\r\n",
                    vin,
                    iin,
                    input_power,
                    converter_get_pot_code(CONVERTER_CHANNEL_MPPT),
                    converter_get_duty(CONVERTER_CHANNEL_MPPT));
        return;
    }

    if (state == MPPT_STATE_FAULT) {
        state = MPPT_STATE_SEARCH;
        have_previous_sample = 0U;
    }

    if (!have_previous_sample) {
        previous_input_power = input_power;
        have_previous_sample = 1U;
        state = MPPT_STATE_SEARCH;
        perturb_output();
        if (state != MPPT_STATE_LIMITED) {
            state = MPPT_STATE_SEARCH;
        }
        converter_apply(CONVERTER_CHANNEL_MPPT, false);
        uart_printf("MPPTDBG state=%s vin=%.3f iin=%.3f pin=%.3f dp=0.000 dir=%d pot=%u duty=%.3f\r\n",
                    mppt_state_to_string(state),
                    vin,
                    iin,
                    input_power,
                    direction,
                    converter_get_pot_code(CONVERTER_CHANNEL_MPPT),
                    converter_get_duty(CONVERTER_CHANNEL_MPPT));
        return;
    }

    delta_power = input_power - previous_input_power;

    if (delta_power < -MPPT_POWER_DEADBAND_W) {
        direction = -direction;
    }

    perturb_output();

    previous_input_power = input_power;

    converter_apply(CONVERTER_CHANNEL_MPPT, state == MPPT_STATE_FAULT);

    uart_printf("MPPTDBG state=%s vin=%.3f iin=%.3f pin=%.3f dp=%.3f dir=%d pot=%u duty=%.3f\r\n",
                mppt_state_to_string(state),
                vin,
                iin,
                input_power,
                delta_power,
                direction,
                converter_get_pot_code(CONVERTER_CHANNEL_MPPT),
                converter_get_duty(CONVERTER_CHANNEL_MPPT));
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
