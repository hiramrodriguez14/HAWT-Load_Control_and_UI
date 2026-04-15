#include "ti_msp_dl_config.h"
#include "drivers/ina229/ina229.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#define UNHANDLED_INTERRUPT()                                                  \
  do {                                                                         \
    __disable_irq();                                                           \
    __BKPT(0);                                                                 \
    while (1) {                                                                \
    }                                                                          \
  } while (0)
#define DELAY (320000)

//Battery State
typedef enum {
    ABSORPTION,
    FLOAT,
    BULK
} battery_state;
// ================= INA CONFIG =================
ina229_t ina = {
    .spi_inst = SPI_0_INST,
    .cs_port = (uint32_t)GPIOA,
    .cs_pin = DL_GPIO_PIN_8,
    .r_shunt_ohms = 0.05f, //Shunt resistance, in our case 50mohms
    .current_lsb = 6.25e-6f,//Expected current / 2^19
    .adc_range = 0 //163.5mV 
};
// ================= VARIABLES =================
uint16_t man_id = 0;
uint16_t dev_id = 0;

float shunt_voltage = 0.0f;
float bus_voltage = 0.0f;
float die_temp = 0.0f;
float current = 0.0f;
float power = 0.0f;
float energy = 0.0f;
float charge = 0.0f;

float Vref;
const float Imax = 0.4; 

uint16_t diagnostics = 0;
volatile uint8_t flags = 0;
int16_t shunt_underVoltage = (int16_t)0x8000;
int16_t shunt_overVoltage  = 0x7FFF;
int16_t bus_underVoltage   = 0;
int16_t bus_overVoltage    = 0x7FFF;
int16_t temperature_limit  = 4000;
int16_t power_limit        = 0x7FFF;


volatile float duty = 0.30f; //duty inicial

const float duty_min = 0.05;
const float duty_max = 0.80f;

volatile float vbat_f = 0.0f; //filtered battery voltage
const float alpha = 0.1f; //simple low-pass filter

volatile uint8_t time_is_up = 0;
volatile uint8_t time_to_uart = 0;

volatile static uint8_t vbat_filter_initialized = 0;

battery_state state = BULK;
// ================= UART =================
void uart_send_char(char c) {
    while (DL_UART_Main_isBusy(UART_0_INST)) {
        ;
    }
    DL_UART_Main_transmitData(UART_0_INST, (uint8_t)c);
}

void uart_send_string(const char *s) {
    while (*s) {
        uart_send_char(*s++);
    }
}

void uart_printf(const char *fmt, ...) {
    char buffer[256];

    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (len < 0) return;

    if (len >= (int)sizeof(buffer)) {
        len = sizeof(buffer) - 1;
    }

    for (int i = 0; i < len; i++) {
        uart_send_char(buffer[i]);
    }
}
// ================= INTERRUPTS =================

void GROUP1_IRQHandler(void) {
    uint32_t pendingB = DL_GPIO_getPendingInterrupt(GPIOB);

    if (pendingB == ALERT_INA229_ALERT_IIDX) {
        DL_GPIO_clearInterruptStatus(GPIOB, ALERT_INA229_ALERT_PIN);
        flags = 1;
    }
}

void  UART_TIMER_INST_IRQHandler(void) {
  switch (DL_Timer_getPendingInterrupt(UART_TIMER_INST)) {
  case DL_TIMER_IIDX_ZERO:
    time_to_uart = 1;
    break;
  default:
    UNHANDLED_INTERRUPT();
    break;
  }
}
void GATE_DRIVING_TIMER_INST_IRQHandler(void) {
  switch (DL_Timer_getPendingInterrupt(GATE_DRIVING_TIMER_INST)) {
  case DL_TIMER_IIDX_ZERO:
    time_is_up = 1;
    break;
  default:
    UNHANDLED_INTERRUPT();
    break;
  }
}
// ================= MAIN =================
int main(void)
{
    SYSCFG_DL_init();
    NVIC_EnableIRQ(GPIOB_INT_IRQn);  
    NVIC_EnableIRQ(UART_TIMER_INST_INT_IRQN);
    NVIC_EnableIRQ(GATE_DRIVING_TIMER_INST_INT_IRQN);
    NVIC_EnableIRQ(KP_BUTTON_INT_IRQN); 
    NVIC_EnableIRQ(ALERT_INT_IRQN); 
 
    DL_Timer_startCounter(UART_TIMER_INST);
    DL_Timer_startCounter(PWM_0_INST);
    DL_Timer_startCounter(GATE_DRIVING_TIMER_INST);
 
    //Configure registers
    if(ina229_write_configuration(&ina, 0xBFF0) != INA229_OK){
        uart_printf("INA229 config register failed\r\n");
        while(1);
    }
    if(ina229_write_adc_configuration(&ina, 0xFB6B) != INA229_OK){
        uart_printf("INA229 adc config register failed\r\n");
        while(1);
    }

    if(ina229_write_shunt_temperature_coefficient(&ina, 0x005A) != INA229_OK){
        uart_printf("INA229 shunt coefficient config register failed\r\n");
        while(1);
    }

    if(ina229_write_shunt_overvoltage_threshold(&ina, shunt_overVoltage) != INA229_OK){
        uart_printf("INA229 shunt overvoltage config register failed\r\n");
        while(1);
    }

    if(ina229_write_shunt_undervoltage_threshold(&ina, shunt_underVoltage) != INA229_OK){
        uart_printf("INA229 shunt undervoltage config register failed\r\n");
        while(1);
    }

    if(ina229_write_bus_overvoltage_threshold(&ina, bus_overVoltage) != INA229_OK){
        uart_printf("INA229 bus overvoltage threshold config register failed\r\n");
        while(1);
    }

    if(ina229_write_bus_undervoltage_threshold(&ina, bus_underVoltage) != INA229_OK){
        uart_printf("INA229 bus undervoltage threshold config register failed\r\n");
        while(1);
    }

    if(ina229_write_power_over_limit_threshold(&ina, power_limit) != INA229_OK){
        uart_printf("INA229 power limit register config register failed\r\n");
        while(1);
    }

    if(ina229_write_temperature_over_limit_threshold(&ina, temperature_limit) != INA229_OK){
        uart_printf("INA229 temperature overlimit config register failed\r\n");
        while(1);
    }

    if(ina229_write_flags(&ina, 0xE001)!= INA229_OK){
        uart_printf("INA229 flags config register failed\r\n");
        while(1);
    }

    //Init
    if (ina229_init(&ina) != INA229_OK) {
        uart_printf("INA229 init failed\r\n");
        while (1);
    }
    
    // IDs
    if (ina229_read_manufacturer_id(&ina, &man_id) == INA229_OK) {
        uart_printf("Manufacturer ID: 0x%04X\r\n", man_id);
    }

    if (ina229_read_device_id(&ina, &dev_id) == INA229_OK) {
        uart_printf("Device ID: 0x%04X\r\n", dev_id);
    }

    ina229_read_flags(&ina, &diagnostics);
    delay_cycles(3200000);

    // ================= LOOP =================

while(1){
    if(flags){

       ina229_read_flags(&ina, &diagnostics);

       if(diagnostics & (1 << 7)){
         uart_printf("Temperature limit exceeded\r\n");
       }
       if(diagnostics & (1 << 6)){
         uart_printf("Shunt voltage upper limit exceeded\r\n");
       }
       if(diagnostics & (1 << 5)){
         uart_printf("Shunt voltage lower limit exceeded\r\n");
       }
       if(diagnostics & (1 << 4)){
         uart_printf("Bus voltage upper limit exceeded\r\n");
       }
       if(diagnostics & (1 << 3)){
         uart_printf("Bus voltage lower limit exceeded\r\n");
       }
       if(diagnostics & (1 << 1)){
         if (ina229_read_shunt_voltage(&ina, &shunt_voltage) == INA229_OK &&
            ina229_read_bus_voltage(&ina, &bus_voltage) == INA229_OK &&
            ina229_read_die_temperature(&ina, &die_temp) == INA229_OK &&
            ina229_read_current(&ina, &current) == INA229_OK &&
            ina229_read_power(&ina, &power) == INA229_OK) {
        } else {
            uart_printf("Read error\r\n");
        }
       }
       flags = 0;
    }

    if(time_to_uart){
        uart_printf(
                "VSHUNT=%.9f V | VBUS=%.6f V | TEMP=%.3f C | I=%.9f A | P=%.9f W\r\n",
                shunt_voltage,
                bus_voltage,
                die_temp,
                current,
                power
            );
        uart_printf("Vref=%.2f vbat_f=%.3f duty=%.3f state=%d\r\n", Vref, vbat_f, duty, state);
        time_to_uart = 0;
    }

    //SEPIC 
    if(time_is_up){
    if(!(ina229_read_bus_voltage(&ina, &bus_voltage) == INA229_OK)){
        uart_printf("Read error\r\n");
    }
    if(!(ina229_read_current(&ina, &current) == INA229_OK)){
        uart_printf("Read error\r\n");
    }

    if (!vbat_filter_initialized) {
        vbat_f = bus_voltage;
        vbat_filter_initialized = 1;
    } else {
        vbat_f = vbat_f + alpha * (bus_voltage - vbat_f);
    }

    float step = 0.002f;
    float deadband = 0.05f;

    if(state == ABSORPTION || state == FLOAT){

    if(state == ABSORPTION){
        Vref = 7.3;
    }else{
        Vref = 6.8;
    }

    if (vbat_f < (Vref - deadband)) {
        duty += step;
    }
    else if (vbat_f > (Vref + deadband)) {
        duty -= step;
    }

    if (duty > duty_max) duty = duty_max;
    if (duty < duty_min) duty = duty_min;

    uint32_t pwm_period = 319;
    uint32_t cmp = (uint32_t)(duty * (pwm_period + 1));

    if (cmp > pwm_period) cmp = pwm_period;

    DL_Timer_setCaptureCompareValue(PWM_0_INST, cmp, GPIO_PWM_0_C0_IDX);

    time_is_up = 0;
    } else { //BULK
    if (current > Imax) {
        duty-=step;
    } else {
        duty+=step;
    }
    if (duty > duty_max) duty = duty_max;
    if (duty < duty_min) duty = duty_min;

    uint32_t pwm_period = 319;
    uint32_t cmp = (uint32_t)(duty * (pwm_period + 1));

    if (cmp > pwm_period) cmp = pwm_period;

    DL_Timer_setCaptureCompareValue(PWM_0_INST, cmp, GPIO_PWM_0_C0_IDX);

    time_is_up = 0;
    }
}
    __WFI(); //Enter LPM and wait for interrupts
}
}