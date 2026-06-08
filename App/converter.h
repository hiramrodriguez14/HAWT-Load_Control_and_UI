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

/*
 * Select the battery charger control method here:
 *   CONVERTER_MODE_PWM_VALUE       -> direct PWM control
 *   CONVERTER_MODE_MCP45HV51_VALUE -> MCP45HV51 digital potentiometer control
 */

#if ((BATTERY_CONVERTER != CONVERTER_MODE_PWM_VALUE) && (BATTERY_CONVERTER != CONVERTER_MODE_MCP45HV51_VALUE))
#error "BATTERY_CONVERTER must be CONVERTER_MODE_PWM_VALUE or CONVERTER_MODE_MCP45HV51_VALUE"
#endif

typedef enum {
    CONVERTER_CHANNEL_BATTERY,
    CONVERTER_CHANNEL_COUNT
} converter_channel_t;

void converter_init(void);
void converter_increase_output(converter_channel_t channel);
void converter_decrease_output(converter_channel_t channel);
void converter_set_output(converter_channel_t channel, float value);
void converter_apply(converter_channel_t channel, bool fault_active);
void converter_disable(converter_channel_t channel);
void converter_set_voltage_reference(converter_channel_t channel, float voltage);
float converter_get_duty(converter_channel_t channel);
float converter_get_voltage_reference(converter_channel_t channel);
uint8_t converter_get_pot_code(converter_channel_t channel);
converter_mode_t converter_get_mode(converter_channel_t channel);
const char *converter_mode_to_string(converter_mode_t mode);

#endif /* CONVERTER_H_ */
