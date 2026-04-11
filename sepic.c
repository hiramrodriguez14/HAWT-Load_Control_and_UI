#include "ti_msp_dl_config.h"
#include "drivers/ina229/ina229.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#define DELAY (320000)

// ================= INA CONFIG =================
ina229_t ina = {
    .spi_inst = SPI_0_INST,
    .cs_port = (uint32_t)GPIOA,
    .cs_pin = DL_GPIO_PIN_8,
    .r_shunt_ohms = 0.3f,
    .current_lsb = 19.1e-6f,
    .adc_range = 1
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

uint16_t diagnostics = 0;
uint8_t flags = 0;
int16_t shunt_underVoltage = (int16_t)0x8000;
int16_t shunt_overVoltage  = 0x7FFF;
int16_t bus_underVoltage   = 0;
int16_t bus_overVoltage    = 0x7FFF;
int16_t temperature_limit  = 4000;
int16_t power_limit        = 0x7FFF;

float Vref = 6.8f;
float Kp = 0.02f;
float Ki = 0.0f;
float Ts = 0.01f; //10ms

float integral = 0.55f;
float duty = 0.30f; //duty inicial

float duty_min = 0.05;
float duty_max = 0.9f;

float vbat_f = 0.0f; //filtered battery voltage
float alpha = 0.1f; //simple low-pass filter

uint8_t time_is_up = 0;
uint8_t time_to_uart = 0;
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
    uint32_t status = DL_GPIO_getEnabledInterruptStatus(GPIOB, ALERT_INA229_ALERT_PIN);

    if (status & ALERT_INA229_ALERT_PIN) {
        DL_GPIO_clearInterruptStatus(GPIOB, ALERT_INA229_ALERT_PIN);
        flags = 1;
        uart_printf("ISR: ALERT interrupt detected\r\n");
    }

    status = DL_GPIO_getEnabledInterruptStatus(GPIOB, GPIO_GRP_0_PIN_0_PIN );
    if(status &  GPIO_GRP_0_PIN_0_PIN ){
        delay_cycles(DELAY);
        DL_GPIO_clearInterruptStatus(GPIOB, GPIO_GRP_0_PIN_0_PIN);
        Kp += 0.1;
    }
}

void TIMA0_IRQHandler(void) {
    uint32_t flags = DL_TimerA_getPendingInterrupt(TIMA0);

    if (flags & DL_TIMERA_INTERRUPT_OVERFLOW_EVENT) {
        DL_TimerA_clearInterruptStatus(TIMA0, DL_TIMERA_INTERRUPT_OVERFLOW_EVENT);

        time_is_up = 1;
    }
}
void TIMA1_IRQHandler(void) {
    uint32_t flags = DL_TimerA_getPendingInterrupt(TIMA1);

    if (flags & DL_TIMERA_INTERRUPT_OVERFLOW_EVENT) {
        DL_TimerA_clearInterruptStatus(TIMA1, DL_TIMERA_INTERRUPT_OVERFLOW_EVENT);

        time_to_uart = 1;
    }
}
// ================= MAIN =================
int main(void)
{
    SYSCFG_DL_init();
    NVIC_EnableIRQ(GPIOB_INT_IRQn);  
    //DL_Timer_setCaptureCompareValue(TIMA0, 10000, DL_TIMERA_CAPTURE_COMPARE_0_INDEX);   //Periodic 300ms interrupt

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
        uart_printf("Kp=%.2f , Ki=%.2f, Integral=%.2f, duty=%.2f",Kp,Ki,integral,duty);
    }

    //SEPIC 
    if(time_is_up){
        //Regulate voltage to 6.8V, this will be the float step
        float vbat;
        float error;
        float output;

        vbat = bus_voltage;

        vbat_f = vbat_f + alpha * (vbat - vbat_f);

        error = Vref - vbat_f;

        integral = integral + (Ki * error * Ts);

        if(integral > duty_max) integral = duty_max;
        if(integral < duty_min) integral = duty_min;

        output = (Kp * error) + integral;

        if(output > duty_max) output = duty_max;
        if(output < duty_min) output = duty_min;

        duty = output;

        uint32_t pwm_period = 320; 
        uint32_t cmp;

        cmp = (uint32_t)(duty * (pwm_period + 1));

        if (cmp > pwm_period) cmp = pwm_period;

        DL_Timer_setCaptureCompareValue(PWM_0_INST, cmp, GPIO_PWM_0_C0_IDX);
        time_is_up = 0;
    }
    __WFI(); //Enter LPM and wait for interrupts
}
}