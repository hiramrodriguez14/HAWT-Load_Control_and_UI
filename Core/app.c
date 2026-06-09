#include "app.h"

#include "ti_msp_dl_config.h"
#include "App/battery_charger.h"
#include "App/converter.h"
#include "App/dump_load.h"
#include "App/load_relay.h"
#include "App/telemetry.h"
#include "App/ui.h"
#include "drivers/uart_debug.h"
#include "drivers/turbine_uart.h"

#include <stdbool.h>
#include <stdint.h>

#define UNHANDLED_INTERRUPT()                                                  \
    do {                                                                       \
        __disable_irq();                                                       \
        __BKPT(0);                                                             \
        while (1) {                                                            \
        }                                                                      \
    } while (0)

#define RECTIFIER_DUMP_LOAD_VOLTAGE_V 14.0f
#define RECTIFIER_CONVERTER_ENABLE_V  3.0f

static volatile uint8_t time_to_update_converters;
static volatile uint8_t time_to_uart;
static uint8_t last_record_enabled;

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

    NVIC_EnableIRQ(UART_TURBINE_INST_INT_IRQN);
    NVIC_EnableIRQ(UART_TIMER_INST_INT_IRQN);
    NVIC_EnableIRQ(CONVERTERS_TIMER_INST_INT_IRQN);
#if defined(GPIO_MULTIPLE_GPIOA_INT_IRQN)
    NVIC_ClearPendingIRQ(GPIO_MULTIPLE_GPIOA_INT_IRQN);
    NVIC_EnableIRQ(GPIO_MULTIPLE_GPIOA_INT_IRQN);
#endif
#if defined(GPIO_MULTIPLE_GPIOB_INT_IRQN)
    NVIC_ClearPendingIRQ(GPIO_MULTIPLE_GPIOB_INT_IRQN);
    NVIC_EnableIRQ(GPIO_MULTIPLE_GPIOB_INT_IRQN);
#elif defined(GPIOB_INT_IRQn)
    NVIC_ClearPendingIRQ(GPIOB_INT_IRQn);
    NVIC_EnableIRQ(GPIOB_INT_IRQn);
#endif
#if defined(TURBINE_UART_INST_INT_IRQN)
    NVIC_ClearPendingIRQ(TURBINE_UART_INST_INT_IRQN);
    NVIC_EnableIRQ(TURBINE_UART_INST_INT_IRQN);
#endif

    DL_Timer_startCounter(UART_TIMER_INST);
    DL_Timer_startCounter(PWM_0_INST);
    DL_Timer_startCounter(CONVERTERS_TIMER_INST);

    // turbine_uart_init();
    load_relay_init();
    dump_load_init();
    converter_init();
    battery_charger_init();
    ui_init();
    converter_disable(CONVERTER_CHANNEL_BATTERY);
    load_relay_disable();
    dump_load_enable();
    uart_turbine_send_condition(true);

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

        if (global_rx_packet_ready) {
            global_rx_packet_ready = false;

            telemetry_update_turbine(global_rx_packet.wind_speed_m_s,
                                     global_rx_packet.hall_effect_rpm,
                                     global_rx_packet.blade_deg,
                                     global_rx_packet.state,
                                    //  global_rx_packet.critical_condition,
                                    //  global_rx_packet.timestamp_valid,
                                     global_rx_packet.calendar.year,
                                     global_rx_packet.calendar.month,
                                     global_rx_packet.calendar.dayOfMonth,
                                     global_rx_packet.calendar.hours,
                                     global_rx_packet.calendar.minutes,
                                     global_rx_packet.calendar.seconds);

        }

        

        if (consume_u8_flag(&time_to_uart)) {
            uint8_t record_enabled;

            ui_update();

            record_enabled = ui_is_record_enabled();
            if (record_enabled && !last_record_enabled) {
                if (!telemetry_log_begin_recording()) {
                    uart_printf("Telemetry log start failed\r\n");
                }
            } else if (!record_enabled && last_record_enabled) {
                telemetry_log_end_recording();
            }

            if (record_enabled) {
                telemetry_log_snapshot();
            }

            last_record_enabled = record_enabled;
        }

        if (consume_u8_flag(&time_to_update_converters)) {
            if (telemetry_sample_battery_control()) {
                bool supervisor_sample_ok = telemetry_sample_power_supervisor();
                const telemetry_snapshot_t *telemetry = telemetry_get_snapshot();
                bool rectifier_over_voltage = supervisor_sample_ok &&
                    (telemetry->rectifier.bus_voltage > RECTIFIER_DUMP_LOAD_VOLTAGE_V);
                bool rectifier_converter_available = supervisor_sample_ok &&
                    (telemetry->rectifier.bus_voltage >= RECTIFIER_CONVERTER_ENABLE_V);
                bool load_critical_condition = rectifier_over_voltage;
                bool turbine_safety_state = telemetry->turbine_packet_valid &&
                    (telemetry->turbine_state == TURBINE_STATE_SAFETY);
                bool turbine_fault_active = load_critical_condition || turbine_safety_state;

                telemetry_set_turbine_critical_condition(load_critical_condition);

                if (ui_is_system_running()) {
                    if (rectifier_over_voltage || turbine_fault_active) {
                        dump_load_enable();
                    } else {
                        dump_load_disable();
                    }

                    uart_turbine_send_condition(load_critical_condition);
                    if (rectifier_converter_available && !turbine_fault_active) {
                        battery_charger_update(telemetry->battery.filtered_bus_voltage,
                                               telemetry->battery.current);
                    } else {
                        converter_disable(CONVERTER_CHANNEL_BATTERY);
                    }

                    if (turbine_fault_active) {
                        load_relay_disable();
                    } else {
                        load_relay_update(telemetry->battery.filtered_bus_voltage);
                    }
                } else {
                    converter_disable(CONVERTER_CHANNEL_BATTERY);
                    load_relay_disable();
                    dump_load_enable();
                    uart_turbine_send_condition(true);
                }
            }
        }

        __WFI();
    }
}

void GROUP1_IRQHandler(void)
{
 
    uint32_t pendingA_iidx = DL_GPIO_getPendingInterrupt(GPIOA);
    uint32_t pendingB_iidx = DL_GPIO_getPendingInterrupt(GPIOB);

    if (pendingB_iidx == RECT_ALERT_PIN_17_IIDX) {
        telemetry_set_rectifier_alert_flag();
    }

    if (pendingB_iidx == BAT_ALERT_PIN_1_IIDX) {
        telemetry_set_battery_alert_flag();
    }

    ui_handle_gpio_interrupt(pendingA_iidx, pendingB_iidx);

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
            break;

        default:
            UNHANDLED_INTERRUPT();
            break;
    }
}
