#include "app.h"

#include "ti_msp_dl_config.h"
#include "App/battery_charger.h"
#include "App/converter.h"
#include "App/load_supervisor.h"
#include "App/load_relay.h"
#include "App/mppt.h"
#include "App/telemetry.h"
#include "App/ui.h"
#include "drivers/uart_debug.h"
#include "drivers/turbine_uart.h"

#include <stdint.h>

#define UNHANDLED_INTERRUPT()                                                  \
    do {                                                                       \
        __disable_irq();                                                       \
        __BKPT(0);                                                             \
        while (1) {                                                            \
        }                                                                      \
    } while (0)

static volatile uint8_t time_to_update_converters;
static volatile uint8_t time_to_uart;
static volatile uint8_t time_to_update_mppt;
static volatile uint8_t mppt_timer_ticks;

static uint8_t consume_u8_flag(volatile uint8_t *flag)
{
    uint8_t local;

    __disable_irq();
    local = *flag;
    *flag = 0;
    __enable_irq();

    return local;
}

void app_init(void)
{
    SYSCFG_DL_init();

    NVIC_EnableIRQ(GPIOB_INT_IRQn);
    NVIC_EnableIRQ(UART_TIMER_INST_INT_IRQN);
    NVIC_EnableIRQ(CONVERTERS_TIMER_INST_INT_IRQN);
#if defined(GPIO_MULTIPLE_GPIOB_INT_IRQN)
    NVIC_EnableIRQ(GPIO_MULTIPLE_GPIOB_INT_IRQN);
#endif
#if defined(UI_BUTTONS_INT_IRQN)
    NVIC_ClearPendingIRQ(UI_BUTTONS_INT_IRQN);
    NVIC_EnableIRQ(UI_BUTTONS_INT_IRQN);
#endif
#if defined(TURBINE_UART_INST_INT_IRQN)
    NVIC_EnableIRQ(TURBINE_UART_INST_INT_IRQN);
#endif

    DL_Timer_startCounter(UART_TIMER_INST);
    DL_Timer_startCounter(PWM_0_INST);
    DL_Timer_startCounter(CONVERTERS_TIMER_INST);

    turbine_uart_init();
    load_relay_init();
    converter_init();
    battery_charger_init();
    mppt_init();
    load_supervisor_init();
    ui_init();

    if (!telemetry_init()) {
        uart_printf("Telemetry init failed\r\n");
        while (1) {
        }
    }
}

void app_run(void)
{
    while (1) {
        telemetry_process_alerts();
        ui_task();

        turbine_uart_sample_t turbine_sample;
        if (turbine_uart_get_latest(&turbine_sample)) {
            telemetry_update_turbine(turbine_sample.wind_speed_m_s,
                                     turbine_sample.rpm,
                                     turbine_sample.state,
                                     turbine_sample.critical_condition);
        }

        if (consume_u8_flag(&time_to_uart)) {
            ui_update();
            if (ui_is_record_enabled()) {
                telemetry_log_snapshot();
            }
        }

        if (consume_u8_flag(&time_to_update_converters)) {
            if (telemetry_sample_battery_control()) {
                const telemetry_snapshot_t *telemetry = telemetry_get_snapshot();

                battery_charger_update(telemetry->battery.filtered_bus_voltage,
                                       telemetry->battery.current);
                load_relay_update(telemetry->battery.filtered_bus_voltage);

                if (ui_is_system_running()) {
                    if (telemetry_sample_power_supervisor()) {
                        telemetry = telemetry_get_snapshot();
                        load_supervisor_update(telemetry->battery.power,
                                               telemetry->rectifier.power);
                    }

                    if (consume_u8_flag(&time_to_update_mppt)) {
                        mppt_update(telemetry->rectifier.bus_voltage,
                                    telemetry->rectifier.current,
                                    telemetry->battery.filtered_bus_voltage,
                                    telemetry->battery.current);
                    }
                } else {
                    load_supervisor_force_mppt_disabled();
                    (void)consume_u8_flag(&time_to_update_mppt);
                }
            }
        }

        __WFI();
    }
}

void GROUP1_IRQHandler(void)
{
    uint32_t pendingA = DL_GPIO_getPendingInterrupt(GPIOA);
    uint32_t pendingB = DL_GPIO_getPendingInterrupt(GPIOB);

    if (pendingB & RECT_ALERT_PIN_2_PIN) {
        DL_GPIO_clearInterruptStatus(RECT_ALERT_PORT, RECT_ALERT_PIN_2_PIN);
        telemetry_set_rectifier_alert_flag();
    }

    if (pendingB & BAT_ALERT_PIN_1_PIN) {
        DL_GPIO_clearInterruptStatus(BAT_ALERT_PORT, BAT_ALERT_PIN_1_PIN);
        telemetry_set_battery_alert_flag();
    }

    ui_handle_gpio_interrupt(pendingA, pendingB);
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

void CONVERTERS_TIMER_INST_IRQHandler(void)
{
    switch (DL_Timer_getPendingInterrupt(CONVERTERS_TIMER_INST)) {
        case DL_TIMER_IIDX_ZERO:
            time_to_update_converters = 1;

            mppt_timer_ticks++;
            if (mppt_timer_ticks >= 20U) {
                mppt_timer_ticks = 0U;
                time_to_update_mppt = 1;
            }
            break;

        default:
            UNHANDLED_INTERRUPT();
            break;
    }
}

#if defined(TURBINE_UART_INST)
void TURBINE_UART_INST_IRQHandler(void)
{
    switch (DL_UART_Main_getPendingInterrupt(TURBINE_UART_INST)) {
        case DL_UART_MAIN_IIDX_RX:
            turbine_uart_on_rx_byte(DL_UART_Main_receiveData(TURBINE_UART_INST));
            break;

        default:
            break;
    }
}
#endif
