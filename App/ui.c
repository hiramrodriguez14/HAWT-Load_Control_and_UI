#include "ui.h"

#include "battery_charger.h"
#include "converter.h"
#include "dump_load.h"
#include "load_relay.h"
#include "load_supervisor.h"
#include "mppt.h"
#include "telemetry.h"
#include "drivers/hd44780/hd44780.h"
#include "drivers/turbine_uart.h"
#include "drivers/uart_debug.h"
#include "ti_msp_dl_config.h"

#include <stdint.h>

#define UI_LCD_COLS        20U
#define UI_LCD_ROWS        4U
#define UI_PAGE_COUNT      5U
#define UI_LOCAL_STATE_RUN  1U
#define UI_LOCAL_STATE_STOP 0U

static volatile uint8_t next_page_event;
static volatile uint8_t prev_page_event;
static volatile uint8_t start_stop_event;
static volatile uint8_t record_event;

static uint8_t current_page;
static uint8_t system_running;
static uint8_t record_enabled;
static uint8_t lcd_dirty;
#if defined(UI_BUTTON_PORT)
static uint8_t next_waiting_release;
static uint8_t prev_waiting_release;
static uint8_t start_stop_waiting_release;
static uint8_t record_waiting_release;

static void handle_spst_button(GPIO_Regs *port,
                               uint32_t pin,
                               volatile uint8_t *event,
                               uint8_t *waiting_release)
{
    uint8_t pressed = (DL_GPIO_readPins(port, pin) & pin) ? 0U : 1U;

    if ((*waiting_release == 0U) && pressed) {
        *event = 1U;
        *waiting_release = 1U;
    } else if ((*waiting_release != 0U) && !pressed) {
        *waiting_release = 0U;
    }
}
#endif

#if defined(LCD_RS_PORT)
static uint8_t lcd_col;

static void lcd_begin_line(uint8_t row)
{
    lcd_col = 0U;
    hd44780_set_cursor(0U, row);
}

static void lcd_print_char_limited(char c)
{
    if (lcd_col < UI_LCD_COLS) {
        hd44780_print_char((uint8_t)c);
        lcd_col++;
    }
}

static void lcd_print_string_limited(const char *text)
{
    while ((*text != '\0') && (lcd_col < UI_LCD_COLS)) {
        hd44780_print_char((uint8_t)*text++);
        lcd_col++;
    }
}

static void lcd_print_u32(uint32_t value)
{
    char digits[10];
    uint8_t count = 0U;

    if (value == 0U) {
        lcd_print_char_limited('0');
        return;
    }

    while ((value > 0U) && (count < sizeof(digits))) {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (count > 0U) {
        lcd_print_char_limited(digits[--count]);
    }
}

static void lcd_print_fixed(float value, uint8_t decimals)
{
    uint32_t scale = 1U;
    int32_t scaled;
    uint32_t whole;
    uint32_t fraction;

    for (uint8_t i = 0U; i < decimals; i++) {
        scale *= 10U;
    }

    scaled = (int32_t)((value * (float)scale) + ((value >= 0.0f) ? 0.5f : -0.5f));

    if (scaled < 0) {
        lcd_print_char_limited('-');
        scaled = -scaled;
    }

    whole = (uint32_t)scaled / scale;
    fraction = (uint32_t)scaled % scale;

    lcd_print_u32(whole);

    if (decimals > 0U) {
        lcd_print_char_limited('.');

        for (uint32_t divisor = scale / 10U; divisor > 0U; divisor /= 10U) {
            lcd_print_char_limited((char)('0' + ((fraction / divisor) % 10U)));
        }
    }
}

static void lcd_finish_line(void)
{
    while (lcd_col < UI_LCD_COLS) {
        hd44780_print_char(' ');
        lcd_col++;
    }
}
#endif

static void set_status_leds(void)
{
#if defined(UI_LED_STOP_PORT) && defined(UI_LED_STOP_PIN)
    if (system_running) {
        DL_GPIO_clearPins(UI_LED_STOP_PORT, UI_LED_STOP_PIN);
    } else {
        DL_GPIO_setPins(UI_LED_STOP_PORT, UI_LED_STOP_PIN);
    }
#endif

#if defined(UI_LED_START_PORT) && defined(UI_LED_START_PIN)
    if (system_running) {
        DL_GPIO_setPins(UI_LED_START_PORT, UI_LED_START_PIN);
    } else {
        DL_GPIO_clearPins(UI_LED_START_PORT, UI_LED_START_PIN);
    }
#endif
}

static void set_record_led(void)
{
#if defined(UI_LED_IS_RECORDING_PORT) && defined(UI_LED_IS_RECORDING_PIN)
    if (record_enabled) {
        DL_GPIO_setPins(UI_LED_IS_RECORDING_PORT, UI_LED_IS_RECORDING_PIN);
    } else {
        DL_GPIO_clearPins(UI_LED_IS_RECORDING_PORT, UI_LED_IS_RECORDING_PIN);
    }
#endif
}

static void set_health_leds(void)
{
    const telemetry_snapshot_t *telemetry = telemetry_get_snapshot();
    uint8_t load_ui_ok = 1U;
    uint8_t turbine_ok = 1U;

    if ((battery_charger_get_state() == BATTERY_CHARGER_FAULT) ||
        (mppt_get_state() == MPPT_STATE_FAULT)) {
        load_ui_ok = 0U;
    }

    if (!telemetry->turbine_packet_valid ||
        telemetry->turbine_critical_condition ||
        (telemetry->turbine_state == TURBINE_STATE_SAFETY)) {
        turbine_ok = 0U;
    }

#if defined(UI_LED_LOAD_UI_OK_PORT) && defined(UI_LED_LOAD_UI_OK_PIN)
    if (load_ui_ok) {
        DL_GPIO_setPins(UI_LED_LOAD_UI_OK_PORT, UI_LED_LOAD_UI_OK_PIN);
    } else {
        DL_GPIO_clearPins(UI_LED_LOAD_UI_OK_PORT, UI_LED_LOAD_UI_OK_PIN);
    }
#endif

#if defined(UI_LED_TURBINE_OK_PORT) && defined(UI_LED_TURBINE_OK_PIN)
    if (turbine_ok) {
        DL_GPIO_setPins(UI_LED_TURBINE_OK_PORT, UI_LED_TURBINE_OK_PIN);
    } else {
        DL_GPIO_clearPins(UI_LED_TURBINE_OK_PORT, UI_LED_TURBINE_OK_PIN);
    }
#endif
}

static void send_turbine_control(void)
{
    turbine_uart_send_control(system_running ? UI_LOCAL_STATE_RUN : UI_LOCAL_STATE_STOP,
                              system_running ? false : true);
}

static uint8_t consume_event(volatile uint8_t *event)
{
    uint8_t local;

    __disable_irq();
    local = *event;
    *event = 0U;
    __enable_irq();

    return local;
}

static void draw_lcd(void)
{
    const telemetry_snapshot_t *telemetry = telemetry_get_snapshot();

#if defined(LCD_RS_PORT)
    switch (current_page) {
        case 0:
            lcd_begin_line(0U);
            lcd_print_string_limited(system_running ? "RUN " : "STOP ");
            lcd_print_string_limited("REC:");
            lcd_print_string_limited(record_enabled ? "ON " : "OFF ");
            lcd_print_string_limited("Pg:");
            lcd_print_u32((uint32_t)current_page + 1U);
            lcd_print_char_limited('/');
            lcd_print_u32(UI_PAGE_COUNT);
            lcd_finish_line();

            lcd_begin_line(1U);
            lcd_print_string_limited("Bat ");
            lcd_print_fixed(telemetry->battery.filtered_bus_voltage, 2U);
            lcd_print_string_limited("V ");
            lcd_print_fixed(telemetry->battery.current, 2U);
            lcd_print_char_limited('A');
            lcd_finish_line();

            lcd_begin_line(2U);
            lcd_print_string_limited("Wind ");
            lcd_print_fixed(telemetry->turbine_wind_speed_m_s, 1U);
            lcd_print_string_limited("m/s RPM");
            lcd_print_fixed(telemetry->turbine_rpm, 0U);
            lcd_finish_line();

            lcd_begin_line(3U);
            lcd_print_string_limited("Chg:");
            lcd_print_string_limited(battery_charger_state_to_string(battery_charger_get_state()));
            lcd_print_string_limited(" M:");
            lcd_print_string_limited(mppt_state_to_string(mppt_get_state()));
            lcd_finish_line();
            break;

        case 1:
            lcd_begin_line(0U);
            lcd_print_string_limited("BATTERY CHARGER");
            lcd_finish_line();

            lcd_begin_line(1U);
            lcd_print_string_limited("Vbat ");
            lcd_print_fixed(telemetry->battery.filtered_bus_voltage, 3U);
            lcd_print_char_limited('V');
            lcd_finish_line();

            lcd_begin_line(2U);
            lcd_print_string_limited("Ibat ");
            lcd_print_fixed(telemetry->battery.current, 3U);
            lcd_print_string_limited("A P");
            lcd_print_fixed(telemetry->battery.power, 2U);
            lcd_print_char_limited('W');
            lcd_finish_line();

            lcd_begin_line(3U);
            lcd_print_string_limited(battery_charger_state_to_string(battery_charger_get_state()));
            lcd_print_string_limited(" Load:");
            lcd_print_string_limited(load_relay_state_to_string(load_relay_get_state()));
            lcd_finish_line();
            break;

        case 2:
            lcd_begin_line(0U);
            lcd_print_string_limited("TURBINE");
            lcd_finish_line();

            lcd_begin_line(1U);
            lcd_print_string_limited("Wind ");
            lcd_print_fixed(telemetry->turbine_wind_speed_m_s, 2U);
            lcd_print_string_limited(" m/s");
            lcd_finish_line();

            lcd_begin_line(2U);
            lcd_print_string_limited("RPM ");
            lcd_print_fixed(telemetry->turbine_rpm, 1U);
            lcd_print_string_limited(" State ");
            lcd_print_u32(telemetry->turbine_state);
            lcd_finish_line();

            lcd_begin_line(3U);
            lcd_print_string_limited("Crit:");
            lcd_print_u32(telemetry->turbine_critical_condition ? 1U : 0U);
            lcd_print_string_limited(" Rx:");
            lcd_print_string_limited(telemetry->turbine_packet_valid ? "OK" : "--");
            lcd_finish_line();
            break;

        case 3:
            lcd_begin_line(0U);
            lcd_print_string_limited("MPPT / RECTIFIER");
            lcd_finish_line();

            lcd_begin_line(1U);
            lcd_print_string_limited("Vrect ");
            lcd_print_fixed(telemetry->rectifier.bus_voltage, 2U);
            lcd_print_string_limited(" I");
            lcd_print_fixed(telemetry->rectifier.current, 2U);
            lcd_finish_line();

            lcd_begin_line(2U);
            lcd_print_string_limited("Prect ");
            lcd_print_fixed(telemetry->rectifier.power, 2U);
            lcd_print_string_limited("W M");
            lcd_print_fixed(load_supervisor_get_power_margin_w(), 2U);
            lcd_finish_line();

            lcd_begin_line(3U);
            lcd_print_string_limited(mppt_state_to_string(mppt_get_state()));
            lcd_print_string_limited(" A:");
            lcd_print_u32(load_supervisor_is_mppt_allowed());
            lcd_print_string_limited(" Dump:");
            lcd_print_string_limited(dump_load_state_to_string(dump_load_get_state()));
            lcd_finish_line();
            break;

        case 4:
        default:
            lcd_begin_line(0U);
            lcd_print_string_limited("SYSTEM");
            lcd_finish_line();

            lcd_begin_line(1U);
            lcd_print_string_limited("Mode:");
            lcd_print_string_limited(system_running ? "START" : "STOP");
            lcd_print_string_limited(" Rec:");
            lcd_print_string_limited(record_enabled ? "ON" : "OFF");
            lcd_finish_line();

            lcd_begin_line(2U);
            lcd_print_string_limited("BatConv:");
            lcd_print_string_limited(converter_mode_to_string(converter_get_mode(CONVERTER_CHANNEL_BATTERY)));
            lcd_finish_line();

            lcd_begin_line(3U);
            lcd_print_string_limited("MpptConv:");
            lcd_print_string_limited(converter_mode_to_string(converter_get_mode(CONVERTER_CHANNEL_MPPT)));
            lcd_finish_line();
            break;
    }
#else
    (void)telemetry;
#endif
}

void ui_init(void)
{
    current_page = 0U;
    system_running = 0U;
    record_enabled = 0U;
    lcd_dirty = 1U;

#if defined(LCD_RS_PORT)
    hd44780_init();
    hd44780_clear_screen();
#endif

    set_status_leds();
    set_record_led();
    set_health_leds();
    send_turbine_control();
    draw_lcd();
}

void ui_task(void)
{
    if (consume_event(&next_page_event)) {
        current_page = (uint8_t)((current_page + 1U) % UI_PAGE_COUNT);
        lcd_dirty = 1U;
    }

    if (consume_event(&prev_page_event)) {
        current_page = (current_page == 0U) ? (UI_PAGE_COUNT - 1U) : (uint8_t)(current_page - 1U);
        lcd_dirty = 1U;
    }

    if (consume_event(&start_stop_event)) {
        system_running = system_running ? 0U : 1U;
        set_status_leds();
        send_turbine_control();
        lcd_dirty = 1U;
    }

    if (consume_event(&record_event)) {
        record_enabled = record_enabled ? 0U : 1U;
        set_record_led();
        lcd_dirty = 1U;
    }

    if (lcd_dirty) {
        lcd_dirty = 0U;
        draw_lcd();
    }
}

void ui_update(void)
{
    lcd_dirty = 1U;
    ui_task();
    set_health_leds();

    const telemetry_snapshot_t *telemetry = telemetry_get_snapshot();

    uart_printf(
        "State=%s | Run=%u | Rec=%u | BatMode=%s | Vbat=%.3f | Ibat=%.6f | BatPot=%u | BatDuty=%.3f | MpptMode=%s | MpptDuty=%.3f | MpptAllowed=%u | PMargin=%.3f | LoadRelay=%s | DumpLoad=%s | Vrect=%.3f | Irect=%.3f | Wind=%.3f | RPM=%.3f | TState=%u | TCrit=%u\r\n",
        battery_charger_state_to_string(battery_charger_get_state()),
        system_running,
        record_enabled,
        converter_mode_to_string(converter_get_mode(CONVERTER_CHANNEL_BATTERY)),
        telemetry->battery.filtered_bus_voltage,
        telemetry->battery.current,
        converter_get_pot_code(CONVERTER_CHANNEL_BATTERY),
        converter_get_duty(CONVERTER_CHANNEL_BATTERY),
        converter_mode_to_string(converter_get_mode(CONVERTER_CHANNEL_MPPT)),
        converter_get_duty(CONVERTER_CHANNEL_MPPT),
        load_supervisor_is_mppt_allowed(),
        load_supervisor_get_power_margin_w(),
        load_relay_state_to_string(load_relay_get_state()),
        dump_load_state_to_string(dump_load_get_state()),
        telemetry->rectifier.bus_voltage,
        telemetry->rectifier.current,
        telemetry->turbine_wind_speed_m_s,
        telemetry->turbine_rpm,
        telemetry->turbine_state,
        telemetry->turbine_critical_condition ? 1U : 0U);
}

void ui_handle_gpio_interrupt(uint32_t gpioa_pending, uint32_t gpiob_pending)
{
#if defined(UI_BUTTON_PORT) && defined(UI_BUTTON_NEXT_PIN)
    if (((UI_BUTTON_PORT == GPIOA) && (gpioa_pending & UI_BUTTON_NEXT_PIN)) ||
        ((UI_BUTTON_PORT == GPIOB) && (gpiob_pending & UI_BUTTON_NEXT_PIN))) {
        DL_GPIO_clearInterruptStatus(UI_BUTTON_PORT, UI_BUTTON_NEXT_PIN);
        handle_spst_button(UI_BUTTON_PORT,
                           UI_BUTTON_NEXT_PIN,
                           &next_page_event,
                           &next_waiting_release);
    }
#endif

#if defined(UI_BUTTON_PORT) && defined(UI_BUTTON_PREV_PIN)
    if (((UI_BUTTON_PORT == GPIOA) && (gpioa_pending & UI_BUTTON_PREV_PIN)) ||
        ((UI_BUTTON_PORT == GPIOB) && (gpiob_pending & UI_BUTTON_PREV_PIN))) {
        DL_GPIO_clearInterruptStatus(UI_BUTTON_PORT, UI_BUTTON_PREV_PIN);
        handle_spst_button(UI_BUTTON_PORT,
                           UI_BUTTON_PREV_PIN,
                           &prev_page_event,
                           &prev_waiting_release);
    }
#endif

#if defined(UI_BUTTON_PORT) && defined(UI_BUTTON_START_STOP_PIN)
    if (((UI_BUTTON_PORT == GPIOA) && (gpioa_pending & UI_BUTTON_START_STOP_PIN)) ||
        ((UI_BUTTON_PORT == GPIOB) && (gpiob_pending & UI_BUTTON_START_STOP_PIN))) {
        DL_GPIO_clearInterruptStatus(UI_BUTTON_PORT, UI_BUTTON_START_STOP_PIN);
        handle_spst_button(UI_BUTTON_PORT,
                           UI_BUTTON_START_STOP_PIN,
                           &start_stop_event,
                           &start_stop_waiting_release);
    }
#endif

#if defined(UI_BUTTON_PORT) && defined(UI_BUTTON_RECORD_PIN)
    if (((UI_BUTTON_PORT == GPIOA) && (gpioa_pending & UI_BUTTON_RECORD_PIN)) ||
        ((UI_BUTTON_PORT == GPIOB) && (gpiob_pending & UI_BUTTON_RECORD_PIN))) {
        DL_GPIO_clearInterruptStatus(UI_BUTTON_PORT, UI_BUTTON_RECORD_PIN);
        handle_spst_button(UI_BUTTON_PORT,
                           UI_BUTTON_RECORD_PIN,
                           &record_event,
                           &record_waiting_release);
    }
#endif
}

unsigned char ui_is_system_running(void)
{
    return system_running;
}

unsigned char ui_is_record_enabled(void)
{
    return record_enabled;
}
