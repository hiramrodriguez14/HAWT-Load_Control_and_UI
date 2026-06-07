#ifndef CONVERTER_H_
#define CONVERTER_H_

#include <stdbool.h>
#include <stdint.h>

#define CONVERTER_MODE_PWM_VALUE        0x00
#define CONVERTER_MODE_MCP45HV51_VALUE  0x01

typedef enum {
    CONVERTER_MODE_PWM = CONVERTER_MODE_PWM_VALUE,
    CONVERTER_MODE_MCP45HV51 = CONVERTER_MODE_MCP45HV51_VALUE
} converter_mode_t;

#define BATTERY_CONVERTER       CONVERTER_MODE_PWM_VALUE
#define MPPT_CONVERTER          CONVERTER_MODE_MCP45HV51_VALUE
#define MPPT_CONVETER           MPPT_CONVERTER

/*
 * These defines select the control method for each logical converter.
 * Current hardware support assumes one MCP45HV51-controlled converter and
 * one direct-PWM converter. Do not assign both app channels to the same
 * control method unless converter.c is extended with a second hardware path.
 */

#if ((BATTERY_CONVERTER != CONVERTER_MODE_PWM_VALUE) && (BATTERY_CONVERTER != CONVERTER_MODE_MCP45HV51_VALUE))
#error "BATTERY_CONVERTER must be CONVERTER_MODE_PWM_VALUE or CONVERTER_MODE_MCP45HV51_VALUE"
#endif

#if ((MPPT_CONVERTER != CONVERTER_MODE_PWM_VALUE) && (MPPT_CONVERTER != CONVERTER_MODE_MCP45HV51_VALUE))
#error "MPPT_CONVERTER must be CONVERTER_MODE_PWM_VALUE or CONVERTER_MODE_MCP45HV51_VALUE"
#endif

#if (BATTERY_CONVERTER == MPPT_CONVERTER)
#error "BATTERY_CONVERTER and MPPT_CONVERTER must map to different hardware paths with the current converter.c"
#endif

typedef enum {
    CONVERTER_CHANNEL_BATTERY,
    CONVERTER_CHANNEL_MPPT,
    CONVERTER_CHANNEL_COUNT
} converter_channel_t;

void converter_init(void);
void converter_increase_output(converter_channel_t channel);
void converter_decrease_output(converter_channel_t channel);
void converter_apply(converter_channel_t channel, bool fault_active);
void converter_disable(converter_channel_t channel);
void converter_set_voltage_reference(converter_channel_t channel, float voltage);
float converter_get_duty(converter_channel_t channel);
float converter_get_voltage_reference(converter_channel_t channel);
uint8_t converter_get_pot_code(converter_channel_t channel);
converter_mode_t converter_get_mode(converter_channel_t channel);
const char *converter_mode_to_string(converter_mode_t mode);

#endif /* CONVERTER_H_ */
