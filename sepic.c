#include "ti_msp_dl_config.h"
#include "drivers/ina229/ina229.h"
#include "drivers/MCP45HV51/mcp45hv51.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>

//These values are constant for a 6V 1.3Ah SLA Battery
#define BATTERY_CONVERTER 0x00 //0x00 means hand made converter, and 0x01 means POT converter
#define MPPT_CONVERTER 0x001 //same as explained above
#define BULK_CURRENT_A          0.20f
#define MAX_CURRENT_A           0.40f

#define ABS_VOLTAGE_V           7.30f
#define ABS_EXIT_CURRENT_A      0.04f

#define FLOAT_VOLTAGE_V         6.80f
#define RESTART_BULK_V          6.30f

#define MAX_BAT_VOLTAGE_V       7.50f
#define MIN_BAT_VOLTAGE_V       4.50f

#define LOAD_DISCONNECT_VOLTAGE_V 5.85f
#define LOAD_RECONNECT_VOLTAGE_V  6.45f


#define DUTY_STEP               0.0005f
#define VOLTAGE_DEADBAND        0.04f

#define UNHANDLED_INTERRUPT()                                                  \
  do {                                                                         \
    __disable_irq();                                                           \
    __BKPT(0);                                                                 \
    while (1) {                                                                \
    }                                                                          \
  } while (0)

typedef enum {
    BULK,
    ABSORPTION,
    FLOAT,
    FAULT
} battery_state;

typedef enum {
    LOAD_DISABLED,
    LOAD_ENABLED
} load_relay_state_t;

ina229_t ina_bat = {
    .spi_inst = SPI_0_INST,
    .cs_port = (uint32_t)BATTERY_CS_PORT,
    .cs_pin = BATTERY_CS_CS1_PIN,
    .r_shunt_ohms = 0.05f,
    .current_lsb = 6.25e-6f,
    .adc_range = 0
};

ina229_t ina_rect = {
    .spi_inst = SPI_1_INST,
    .cs_port = (uint32_t)RECT_CS_PORT,
    .cs_pin = RECT_CS_CS2_PIN,
    .r_shunt_ohms = 0.05f,
    .current_lsb = 6.25e-6f,
    .adc_range = 0
};


uint16_t bat_man_id = 0;
uint16_t bat_dev_id = 0;

float bat_shunt_voltage = 0.0f;
float bat_bus_voltage = 0.0f;
float bat_die_temp = 0.0f;
float bat_current = 0.0f;
float bat_power = 0.0f;
float bat_energy = 0.0f;
float bat_charge = 0.0f;
uint16_t bat_diagnostics = 0;
volatile uint8_t bat_flags = 0;

uint16_t rect_man_id = 0;
uint16_t rect_dev_id = 0;

float rect_shunt_voltage = 0.0f;
float rect_bus_voltage = 0.0f;
float rect_die_temp = 0.0f;
float rect_current = 0.0f;
float rect_power = 0.0f;
float rect_energy = 0.0f;
float rect_charge = 0.0f;
uint16_t rect_diagnostics = 0;
volatile uint8_t rect_flags = 0;

float Vref = 0.0f;

int16_t shunt_underVoltage = (int16_t)0x8000;
int16_t shunt_overVoltage  = 0x7FFF;
int16_t bus_underVoltage   = 0;
int16_t bus_overVoltage    = 0x7FFF;
int16_t temperature_limit  = 4000;
int16_t power_limit        = 0x7FFF;

float duty = 0.30f;
bool pot = false;

const float output_min = 0.05f;
const float output_max = 0.99f;

#define CURRENT_DEADBAND_A      0.1f
#define SAFE_OFF_POT_CODE       255U
#define MCP45HV51_WAIT_TIME 10
volatile float vbat_f = 0.0f;
const float alpha = 0.1f;

volatile uint8_t time_is_up = 0;
volatile uint8_t time_to_uart = 0;
volatile uint8_t vbat_filter_initialized = 0;

static uint8_t last_pot_code = 0xFF;
static uint8_t control_divider = 0;
volatile load_relay_state_t load_relay_state = LOAD_DISABLED;

battery_state state = BULK;

void uart_send_char(char c)
{
    while (DL_UART_Main_isBusy(UART_0_INST)) {
        ;
    }

    DL_UART_Main_transmitData(UART_0_INST, (uint8_t)c);
}

void uart_send_string(const char *s)
{
    while (*s) {
        uart_send_char(*s++);
    }
}

void uart_printf(const char *fmt, ...)
{
    char buffer[256];

    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (len < 0) {
        return;
    }

    if (len >= (int)sizeof(buffer)) {
        len = sizeof(buffer) - 1;
    }

    for (int i = 0; i < len; i++) {
        uart_send_char(buffer[i]);
    }
}

const char *battery_state_to_string(battery_state s)
{
    switch (s)
    {
        case BULK:
            return "BULK";

        case ABSORPTION:
            return "ABSORPTION";

        case FLOAT:
            return "FLOAT";

        case FAULT:
            return "FAULT";

        default:
            return "UNKNOWN";
    }
}

void update_pwm_from_value(float value)
{
    uint32_t pwm_period = 319;
    uint32_t cmp = (uint32_t)(value * (pwm_period + 1));

    if (cmp > pwm_period) {
        cmp = pwm_period;
    }

    DL_Timer_setCaptureCompareValue(
        PWM_0_INST,
        cmp,
        GPIO_PWM_0_C0_IDX
    );
}

/*
 * Load relay behavior:
 *   - The relay is NOT part of the charger power path anymore.
 *   - The charger stays connected to the battery through the INA229.
 *   - This relay only enables/disables external battery loads.
 *
 *   SET relay GPIO = load connected/enabled
 *   CLEAR relay GPIO   = load disconnected/disabled
 */
void load_relay_enable(void)
{
    DL_GPIO_setPins(RELAY_PORT, RELAY_IN_PIN );
    load_relay_state = LOAD_ENABLED;
}

void load_relay_disable(void)
{
    DL_GPIO_clearPins(RELAY_PORT, RELAY_IN_PIN );
    load_relay_state = LOAD_DISABLED;
}

void load_relay_update(float vbat)
{
    if (vbat <= LOAD_DISCONNECT_VOLTAGE_V) {
        load_relay_disable();
    }
    else if (vbat >= LOAD_RECONNECT_VOLTAGE_V) {
        load_relay_enable();
    }
}

const char *load_relay_state_to_string(load_relay_state_t s)
{
    return (s == LOAD_ENABLED) ? "ON" : "OFF";
}

void converter_disable(void)
{
    pot = 0.0f;
    duty = 0.0f;
    update_pwm_from_value(0.0f);

    if (BATTERY_CONVERTER) {
        if (last_pot_code != SAFE_OFF_POT_CODE) {
            if (!MCP45HV51_setWiperRaw(SAFE_OFF_POT_CODE)) {
                uart_printf("MCP45HV51 safe-off write failed\r\n");
            }
            last_pot_code = SAFE_OFF_POT_CODE;
        }
    }
}

void apply_converter_output(void)
{
    if (state == FAULT) {
        converter_disable();
        return;
    }

    if (!BATTERY_CONVERTER) {
        update_pwm_from_value(duty);
    }
    else {
        //already updated in charger control function
        duty = 0.0f;
        update_pwm_from_value(0.0f);
    }
}

void update_battery_state(float vbat, float ibat)
{
    if (vbat > MAX_BAT_VOLTAGE_V || ibat > MAX_CURRENT_A) {
        state = FAULT;
        converter_disable();
        return;
    }

    switch (state)
    {
        case BULK:
            if (vbat >= ABS_VOLTAGE_V) {
                state = ABSORPTION;
            }
            break;

        case ABSORPTION:
            if (ibat <= ABS_EXIT_CURRENT_A &&
                vbat >= ABS_VOLTAGE_V)
            {
                state = FLOAT;
            }
            break;

        case FLOAT:
            if (vbat <= RESTART_BULK_V) {
                state = BULK;
            }
            break;

        case FAULT:
            converter_disable();

            if (vbat < ABS_VOLTAGE_V &&
                ibat < BULK_CURRENT_A)
            {
                state = BULK;
            }
            break;
    }
}

void charger_control_update(float vbat, float ibat)
{
    update_battery_state(vbat, ibat);

    /*
     * PWM mode:
     *   More control_output -> more duty -> usually more converter output.
     *
     * MCP45HV51 mode:
     *   More pot code -> more resistance -> lower regulated voltage.
     *   Therefore the control action must be inverted.
     */
    float step = DUTY_STEP;


    switch (state)
    {
        case BULK:
            Vref = ABS_VOLTAGE_V;

            if (ibat < (BULK_CURRENT_A)) {
                if(BATTERY_CONVERTER){
                    if(control_divider>=MCP45HV51_WAIT_TIME){
                    pot = MCP45HV51_decrementWiper();
                    if (pot) last_pot_code--;
                    control_divider = 0;
                    }else{
                        control_divider++;
                    }
                }else{
                duty += step;
                }
            }
            else if (ibat > (BULK_CURRENT_A + CURRENT_DEADBAND_A)) {
                if(BATTERY_CONVERTER){
                    if(control_divider>=MCP45HV51_WAIT_TIME){
                    pot = MCP45HV51_incrementWiper();
                    if (pot) last_pot_code++;
                    control_divider = 0;
                    }else{
                        control_divider++;
                    }
                }else{
                duty -= step;
                }
            }
            break;

        case ABSORPTION:
            Vref = ABS_VOLTAGE_V;

            if (vbat < (Vref - VOLTAGE_DEADBAND)) {
              if(BATTERY_CONVERTER){
                if(control_divider>=MCP45HV51_WAIT_TIME){
                    pot = MCP45HV51_decrementWiper();
                    if (pot) last_pot_code--;
                    control_divider = 0;
                }else{
                    control_divider++;
                }
                }else{
                duty += step;
                }
            }
            else if (vbat > (Vref + VOLTAGE_DEADBAND)) {
                if(BATTERY_CONVERTER){
                    if(control_divider>=MCP45HV51_WAIT_TIME){
                    pot = MCP45HV51_incrementWiper();
                    if (pot) last_pot_code++;
                    control_divider = 0;
                    }else{
                        control_divider++;
                    }
                }else{
                duty -= step;
                }
            }
            break;

        case FLOAT:
            Vref = FLOAT_VOLTAGE_V;

            if (vbat < (Vref - VOLTAGE_DEADBAND)) {
              if(BATTERY_CONVERTER){
                if(control_divider >= MCP45HV51_WAIT_TIME){
                    pot = MCP45HV51_decrementWiper();
                    if (pot) last_pot_code--;
                    control_divider = 0;
                }else{
                    control_divider++;
                }
                }else{
                duty += step;
                }
            }
            else if (vbat > (Vref + VOLTAGE_DEADBAND)) {
                 if(BATTERY_CONVERTER){
                    if(control_divider >= MCP45HV51_WAIT_TIME){
                    pot = MCP45HV51_incrementWiper();
                     if (pot) last_pot_code++;
                     control_divider = 0;
                    }else{
                        control_divider++;
                    }
                }else{
                duty -= step;
                }
            }
            break;

        case FAULT:
            converter_disable();
            break;
    }

    if (state != FAULT)
    {
        if (duty > output_max) {
            duty = output_max;
        }

        if (duty < output_min) {
            duty = output_min;
        }
    }
    else {
        converter_disable();
    }

    apply_converter_output();
}

void GROUP1_IRQHandler(void)
{
    uint32_t pendingB = DL_GPIO_getPendingInterrupt(GPIOB);

    if (pendingB & RECT_ALERT_PIN_2_PIN) {
        DL_GPIO_clearInterruptStatus(
            RECT_ALERT_PORT,
            RECT_ALERT_PIN_2_PIN
        );

        rect_flags = 1;
    }

    if (pendingB & BAT_ALERT_PIN_1_PIN) {
        DL_GPIO_clearInterruptStatus(
            BAT_ALERT_PORT,
            BAT_ALERT_PIN_1_PIN
        );

        bat_flags = 1;
    }
}

void UART_TIMER_INST_IRQHandler(void)
{
    switch (DL_Timer_getPendingInterrupt(UART_TIMER_INST)) {
        case DL_TIMER_IIDX_ZERO:
            time_to_uart = 1;
            break;

        default:
            UNHANDLED_INTERRUPT();
            break;
    }
}

void GATE_DRIVING_TIMER_INST_IRQHandler(void)
{
    switch (DL_Timer_getPendingInterrupt(GATE_DRIVING_TIMER_INST)) {
        case DL_TIMER_IIDX_ZERO:
            time_is_up = 1;
            break;

        default:
            UNHANDLED_INTERRUPT();
            break;
    }
}

static uint8_t consume_u8_flag(volatile uint8_t *flag)
{
    uint8_t local;

    __disable_irq();
    local = *flag;
    *flag = 0;
    __enable_irq();

    return local;
}

int main(void)
{
    SYSCFG_DL_init();

    NVIC_EnableIRQ(GPIOB_INT_IRQn);
    NVIC_EnableIRQ(UART_TIMER_INST_INT_IRQN);
    NVIC_EnableIRQ(GATE_DRIVING_TIMER_INST_INT_IRQN);
    NVIC_EnableIRQ(GPIO_MULTIPLE_GPIOB_INT_IRQN);

    DL_Timer_startCounter(UART_TIMER_INST);
    DL_Timer_startCounter(PWM_0_INST);
    DL_Timer_startCounter(GATE_DRIVING_TIMER_INST);

    load_relay_disable();
    update_pwm_from_value(0.0f);

    if (!MCP45HV51_init()) {
        uart_printf("MCP45HV51 init failed\r\n");
        while (1);
    }

    if (!MCP45HV51_setWiperRaw((uint8_t)(0.3f * 255.0f))) {
        uart_printf("MCP45HV51 initial wiper failed\r\n");
        while (1);
    }

    last_pot_code = (uint8_t)(0.3f * 255.0f);

    if (ina229_write_configuration(&ina_bat, 0xBFF0) != INA229_OK) {
        uart_printf("INA229 battery config register failed\r\n");
        while (1);
    }

    if (ina229_write_adc_configuration(&ina_bat, 0xFB6B) != INA229_OK) {
        uart_printf("INA229 battery adc config register failed\r\n");
        while (1);
    }

    if (ina229_write_shunt_temperature_coefficient(&ina_bat, 0x005A) != INA229_OK) {
        uart_printf("INA229 battery shunt coefficient config failed\r\n");
        while (1);
    }

    if (ina229_write_shunt_overvoltage_threshold(&ina_bat, shunt_overVoltage) != INA229_OK) {
        uart_printf("INA229 battery shunt overvoltage config failed\r\n");
        while (1);
    }

    if (ina229_write_shunt_undervoltage_threshold(&ina_bat, shunt_underVoltage) != INA229_OK) {
        uart_printf("INA229 battery shunt undervoltage config failed\r\n");
        while (1);
    }

    if (ina229_write_bus_overvoltage_threshold(&ina_bat, bus_overVoltage) != INA229_OK) {
        uart_printf("INA229 battery bus overvoltage config failed\r\n");
        while (1);
    }

    if (ina229_write_bus_undervoltage_threshold(&ina_bat, bus_underVoltage) != INA229_OK) {
        uart_printf("INA229 battery bus undervoltage config failed\r\n");
        while (1);
    }

    if (ina229_write_power_over_limit_threshold(&ina_bat, power_limit) != INA229_OK) {
        uart_printf("INA229 battery power limit config failed\r\n");
        while (1);
    }

    if (ina229_write_temperature_over_limit_threshold(&ina_bat, temperature_limit) != INA229_OK) {
        uart_printf("INA229 battery temperature limit config failed\r\n");
        while (1);
    }

    if (ina229_write_flags(&ina_bat, 0xE001) != INA229_OK) {
        uart_printf("INA229 battery flags config failed\r\n");
        while (1);
    }

    if (ina229_write_configuration(&ina_rect, 0xBFF0) != INA229_OK) {
        uart_printf("INA229 rectifier config register failed\r\n");
        while (1);
    }

    if (ina229_write_adc_configuration(&ina_rect, 0xFB6B) != INA229_OK) {
        uart_printf("INA229 rectifier adc config register failed\r\n");
        while (1);
    }

    if (ina229_write_shunt_temperature_coefficient(&ina_rect, 0x005A) != INA229_OK) {
        uart_printf("INA229 rectifier shunt coefficient config failed\r\n");
        while (1);
    }

    if (ina229_write_shunt_overvoltage_threshold(&ina_rect, shunt_overVoltage) != INA229_OK) {
        uart_printf("INA229 rectifier shunt overvoltage config failed\r\n");
        while (1);
    }

    if (ina229_write_shunt_undervoltage_threshold(&ina_rect, shunt_underVoltage) != INA229_OK) {
        uart_printf("INA229 rectifier shunt undervoltage config failed\r\n");
        while (1);
    }

    if (ina229_write_bus_overvoltage_threshold(&ina_rect, bus_overVoltage) != INA229_OK) {
        uart_printf("INA229 rectifier bus overvoltage config failed\r\n");
        while (1);
    }

    if (ina229_write_bus_undervoltage_threshold(&ina_rect, bus_underVoltage) != INA229_OK) {
        uart_printf("INA229 rectifier bus undervoltage config failed\r\n");
        while (1);
    }

    if (ina229_write_power_over_limit_threshold(&ina_rect, power_limit) != INA229_OK) {
        uart_printf("INA229 rectifier power limit config failed\r\n");
        while (1);
    }

    if (ina229_write_temperature_over_limit_threshold(&ina_rect, temperature_limit) != INA229_OK) {
        uart_printf("INA229 rectifier temperature limit config failed\r\n");
        while (1);
    }

    if (ina229_write_flags(&ina_rect, 0xE001) != INA229_OK) {
        uart_printf("INA229 rectifier flags config failed\r\n");
        while (1);
    }

    if (ina229_init(&ina_bat) != INA229_OK) {
        uart_printf("INA229 battery init failed\r\n");
        while (1);
    }

    if (ina229_read_manufacturer_id(&ina_bat, &bat_man_id) == INA229_OK) {
        uart_printf("Battery Manufacturer ID: 0x%04X\r\n", bat_man_id);
    }

    if (ina229_read_device_id(&ina_bat, &bat_dev_id) == INA229_OK) {
        uart_printf("Battery Device ID: 0x%04X\r\n", bat_dev_id);
    }

    ina229_read_flags(&ina_bat, &bat_diagnostics);
    delay_cycles(3200000U);

    if (ina229_init(&ina_rect) != INA229_OK) {
        uart_printf("INA229 rectifier init failed\r\n");
        while (1);
    }

    if (ina229_read_manufacturer_id(&ina_rect, &rect_man_id) == INA229_OK) {
        uart_printf("Rectifier Manufacturer ID: 0x%04X\r\n", rect_man_id);
    }

    if (ina229_read_device_id(&ina_rect, &rect_dev_id) == INA229_OK) {
        uart_printf("Rectifier Device ID: 0x%04X\r\n", rect_dev_id);
    }

    ina229_read_flags(&ina_rect, &rect_diagnostics);
    delay_cycles(3200000U);

    while (1)
    {
        if (consume_u8_flag(&bat_flags)) {
            ina229_read_flags(&ina_bat, &bat_diagnostics);

            if (bat_diagnostics & (1 << 7)) {
                uart_printf("Battery temperature limit exceeded\r\n");
            }

            if (bat_diagnostics & (1 << 6)) {
                uart_printf("Battery shunt voltage upper limit exceeded\r\n");
            }

            if (bat_diagnostics & (1 << 5)) {
                uart_printf("Battery shunt voltage lower limit exceeded\r\n");
            }

            if (bat_diagnostics & (1 << 4)) {
                uart_printf("Battery bus voltage upper limit exceeded\r\n");
            }

            if (bat_diagnostics & (1 << 3)) {
                uart_printf("Battery Bus voltage lower limit exceeded\r\n");
            }

            if (bat_diagnostics & (1 << 1)) {
                if (ina229_read_shunt_voltage(&ina_bat, &bat_shunt_voltage) == INA229_OK &&
                    ina229_read_bus_voltage(&ina_bat, &bat_bus_voltage) == INA229_OK &&
                    ina229_read_die_temperature(&ina_bat, &bat_die_temp) == INA229_OK &&
                    ina229_read_current(&ina_bat, &bat_current) == INA229_OK &&
                    ina229_read_power(&ina_bat, &bat_power) == INA229_OK) {
                }
                else {
                    uart_printf("Battery read error\r\n");
                }
            }

        }

        if (consume_u8_flag(&rect_flags)) {
            ina229_read_flags(&ina_rect, &rect_diagnostics);

            if (rect_diagnostics & (1 << 7)) {
                uart_printf("Rectifier temperature limit exceeded\r\n");
            }

            if (rect_diagnostics & (1 << 6)) {
                uart_printf("Rectifier shunt voltage upper limit exceeded\r\n");
            }

            if (rect_diagnostics & (1 << 5)) {
                uart_printf("Rectifier shunt voltage lower limit exceeded\r\n");
            }

            if (rect_diagnostics & (1 << 4)) {
                uart_printf("Rectifier bus voltage upper limit exceeded\r\n");
            }

            if (rect_diagnostics & (1 << 3)) {
                uart_printf("Rectifier bus voltage lower limit exceeded\r\n");
            }

            if (rect_diagnostics & (1 << 1)) {
                if (ina229_read_shunt_voltage(&ina_rect, &rect_shunt_voltage) == INA229_OK &&
                    ina229_read_bus_voltage(&ina_rect, &rect_bus_voltage) == INA229_OK &&
                    ina229_read_die_temperature(&ina_rect, &rect_die_temp) == INA229_OK &&
                    ina229_read_current(&ina_rect, &rect_current) == INA229_OK &&
                    ina229_read_power(&ina_rect, &rect_power) == INA229_OK) {
                }
                else {
                    uart_printf("Rectifier read error\r\n");
                }
            }

        }

        if (consume_u8_flag(&time_to_uart)) {
            uart_printf(
            "State=%s | Mode=%s | Vbat=%.3f | Ibat=%.6f | PotCode=%u | Duty=%.3f | LoadRelay=%s | Vrect=%.3f | Irect=%.3f\r\n",
            battery_state_to_string(state),
            select_converter ? "MCP45HV51" : "PWM",
            vbat_f,
            bat_current,
            last_pot_code,
            duty,
            load_relay_state_to_string(load_relay_state),
            rect_bus_voltage,
            rect_current
            );

        }

        if (consume_u8_flag(&time_is_up)) {
            if (ina229_read_bus_voltage(&ina_bat, &bat_bus_voltage) != INA229_OK) {
                uart_printf("Battery voltage read error\r\n");
            }

            if (ina229_read_current(&ina_bat, &bat_current) != INA229_OK) {
                uart_printf("Battery current read error\r\n");
            }

            if (!vbat_filter_initialized) {
                vbat_f = bat_bus_voltage;
                vbat_filter_initialized = 1;
            }
            else {
                vbat_f = vbat_f + alpha * (bat_bus_voltage - vbat_f); //filter
            }

            charger_control_update(vbat_f, bat_current);
            load_relay_update(vbat_f);
        }

        __WFI();
    }
}
