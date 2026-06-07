#include "converter.h"

#include "ti_msp_dl_config.h"
#include "drivers/MCP45HV51/mcp45hv51.h"
#include "drivers/uart_debug.h"

#define DUTY_STEP               0.0005f
#define OUTPUT_MIN              0.05f
#define OUTPUT_MAX              0.99f
#define SAFE_OFF_POT_CODE       255U
#define MCP45HV51_WAIT_TIME     10U

typedef struct {
    converter_mode_t mode;
    float duty;
    float voltage_reference;
    uint8_t last_pot_code;
    uint8_t control_divider;
} converter_state_t;

static converter_state_t converters[CONVERTER_CHANNEL_COUNT] = {
    [CONVERTER_CHANNEL_BATTERY] = {
        .mode = BATTERY_CONVERTER,
        .duty = 0.30f,
        .last_pot_code = 0xFFU
    },
    [CONVERTER_CHANNEL_MPPT] = {
        .mode = MPPT_CONVERTER,
        .duty = 0.30f,
        .last_pot_code = 0xFFU
    }
};

static converter_state_t *get_converter(converter_channel_t channel)
{
    if (channel >= CONVERTER_CHANNEL_COUNT) {
        return &converters[CONVERTER_CHANNEL_BATTERY];
    }

    return &converters[channel];
}

static void update_pwm_from_value(converter_channel_t channel, float value)
{
    uint32_t pwm_period = 319U;
    uint32_t cmp = (uint32_t)(value * (float)(pwm_period + 1U));

    if (cmp > pwm_period) {
        cmp = pwm_period;
    }

    switch (channel) {
        case CONVERTER_CHANNEL_MPPT:
            DL_Timer_setCaptureCompareValue(PWM_0_INST, cmp, GPIO_PWM_0_C0_IDX);
            break;

        case CONVERTER_CHANNEL_BATTERY:
        default:
            DL_Timer_setCaptureCompareValue(PWM_0_INST, cmp, GPIO_PWM_0_C0_IDX);
            break;
    }
}

static bool pot_step_ready(converter_state_t *converter)
{
    if (converter->control_divider >= MCP45HV51_WAIT_TIME) {
        converter->control_divider = 0U;
        return true;
    }

    converter->control_divider++;
    return false;
}

void converter_init(void)
{
    update_pwm_from_value(CONVERTER_CHANNEL_BATTERY, 0.0f);
    update_pwm_from_value(CONVERTER_CHANNEL_MPPT, 0.0f);

    if (((converter_mode_t)BATTERY_CONVERTER != CONVERTER_MODE_MCP45HV51) &&
        ((converter_mode_t)MPPT_CONVERTER != CONVERTER_MODE_MCP45HV51)) {
        return;
    }

    if (!MCP45HV51_init()) {
        uart_printf("MCP45HV51 init failed\r\n");
        while (1) {
        }
    }

    MCP45HV51_setWiperRaw(0);
    delay_cycles(32000000U);
    MCP45HV51_setWiperRaw(128);
    delay_cycles(32000000U);
    MCP45HV51_setWiperRaw(255);
    
    if (converters[CONVERTER_CHANNEL_BATTERY].mode == CONVERTER_MODE_MCP45HV51) {
        converters[CONVERTER_CHANNEL_BATTERY].last_pot_code = (uint8_t)(0.3f * 255.0f);
    }

    if (converters[CONVERTER_CHANNEL_MPPT].mode == CONVERTER_MODE_MCP45HV51) {
        converters[CONVERTER_CHANNEL_MPPT].last_pot_code = (uint8_t)(0.3f * 255.0f);
    }

    if (!MCP45HV51_setWiperRaw((uint8_t)(0.3f * 255.0f))) {
        uart_printf("MCP45HV51 initial wiper failed\r\n");
        while (1) {
        }
    }
}

void converter_increase_output(converter_channel_t channel)
{
    converter_state_t *converter = get_converter(channel);

    if (converter->mode == CONVERTER_MODE_MCP45HV51) {
        if (pot_step_ready(converter) && MCP45HV51_decrementWiper() && (converter->last_pot_code > 0U)) {
            converter->last_pot_code--;
        }
    } else {
        converter->duty += DUTY_STEP;
    }
}

void converter_decrease_output(converter_channel_t channel)
{
    converter_state_t *converter = get_converter(channel);

    if (converter->mode == CONVERTER_MODE_MCP45HV51) {
        if (pot_step_ready(converter) && MCP45HV51_incrementWiper() && (converter->last_pot_code < SAFE_OFF_POT_CODE)) {
            converter->last_pot_code++;
        }
    } else {
        converter->duty -= DUTY_STEP;
    }
}

void converter_apply(converter_channel_t channel, bool fault_active)
{
    converter_state_t *converter = get_converter(channel);

    if (fault_active) {
        converter_disable(channel);
        return;
    }

    if (converter->duty > OUTPUT_MAX) {
        converter->duty = OUTPUT_MAX;
    }

    if (converter->duty < OUTPUT_MIN) {
        converter->duty = OUTPUT_MIN;
    }

    if (converter->mode == CONVERTER_MODE_PWM) {
        update_pwm_from_value(channel, converter->duty);
    } else {
        converter->duty = 0.0f;
    }
}

void converter_disable(converter_channel_t channel)
{
    converter_state_t *converter = get_converter(channel);

    converter->duty = 0.0f;

    if (converter->mode == CONVERTER_MODE_PWM) {
        update_pwm_from_value(channel, 0.0f);
    }

    if (converter->mode == CONVERTER_MODE_MCP45HV51) {
        if (converter->last_pot_code != SAFE_OFF_POT_CODE) {
            if (!MCP45HV51_setWiperRaw(SAFE_OFF_POT_CODE)) {
                uart_printf("MCP45HV51 safe-off write failed\r\n");
            }
            converter->last_pot_code = SAFE_OFF_POT_CODE;
        }
    }
}

void converter_set_voltage_reference(converter_channel_t channel, float voltage)
{
    get_converter(channel)->voltage_reference = voltage;
}

float converter_get_duty(converter_channel_t channel)
{
    return get_converter(channel)->duty;
}

float converter_get_voltage_reference(converter_channel_t channel)
{
    return get_converter(channel)->voltage_reference;
}

uint8_t converter_get_pot_code(converter_channel_t channel)
{
    return get_converter(channel)->last_pot_code;
}

converter_mode_t converter_get_mode(converter_channel_t channel)
{
    return get_converter(channel)->mode;
}

const char *converter_mode_to_string(converter_mode_t converter_mode)
{
    return (converter_mode == CONVERTER_MODE_MCP45HV51) ? "MCP45HV51" : "PWM";
}
