#include "ti_msp_dl_config.h"
#include "drivers/ina229/ina229.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#define DELAY (1600000) //5ms delay

// ================= INA CONFIG =================
ina229_t ina = {
    .spi_inst = SPI_0_INST,
    .cs_port = (uint32_t)GPIOA,
    .cs_pin = DL_GPIO_PIN_8,
    .r_shunt_ohms = 0.3f,
    .current_lsb = 1.97e-6f,
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
void GROUP1_IRQHandler(void){
    uint32_t status = DL_GPIO_getEnabledInterruptStatus(GPIOB, ALERT_INA229_ALERT_PIN);
    if(status & ALERT_INA229_ALERT_PIN){
        delay_cycles(DELAY);
        DL_GPIO_clearInterruptStatus(GPIOB, ALERT_INA229_ALERT_PIN);

        flags = 1;
    }
}
// ================= MAIN =================
int main(void)
{
    SYSCFG_DL_init();


    //Configure registers
    if(ina229_write_configuration(&ina, 0xBFF0) != INA229_OK){
        uart_printf("INA229 config register failed\r\n");
        while(1);
    }
    if(ina229_write_adc_configuration(&ina, 0xF553) != INA229_OK){
        uart_printf("INA229 adc config register failed\r\n");
        while(1);
    }

    if(ina229_write_shunt_temperature_coefficient(&ina, 0x005A) != INA229_OK){
        uart_printf("INA229 shunt coefficient config register failed\r\n");
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

            uart_printf(
                "VSHUNT=%.9f V | VBUS=%.6f V | TEMP=%.3f C | I=%.9f A | P=%.9f W\r\n",
                shunt_voltage,
                bus_voltage,
                die_temp,
                current,
                power
            );

        } else {
            uart_printf("Read error\r\n");
        }
       }
       flags = 0;
    }
    __disable_irq();   // deshabilita globalmente
    delay_cycles(32000000);
    __enable_irq();    // habilita globalmente
    __WFI(); //Enter LPM and wait for interrupts
}
}