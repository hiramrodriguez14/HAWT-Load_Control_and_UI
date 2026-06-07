#include "telemetry.h"

#include "ti_msp_dl_config.h"
#include "drivers/ina229/ina229.h"
#include "drivers/sd_card/ff.h"
#include "drivers/uart_debug.h"

#include <stdio.h>
#include <string.h>

#define VBAT_FILTER_ALPHA 0.1f
#define TELEMETRY_LOG_FILE "TLOG.CSV"

static ina229_t ina_bat = {
    .spi_inst = INA229_BATTERY_SPI1_INST,
    .cs_port = (uint32_t)BATTERY_CS_PORT,
    .cs_pin = BATTERY_CS_CS1_PIN,
    .r_shunt_ohms = 0.015f,
    .current_lsb = 6.25e-6f,
    .adc_range = 0
};

static ina229_t ina_rect = {
    .spi_inst = INA229_RECT_SPI0_INST,
    .cs_port = (uint32_t)RECT_CS_PORT,
    .cs_pin = RECT_CS_CS2_PIN,
    .r_shunt_ohms = 0.015f,
    .current_lsb = 6.25e-6f,
    .adc_range = 0
};

static telemetry_snapshot_t snapshot;
static volatile uint8_t bat_flags;
static volatile uint8_t rect_flags;
static bool vbat_filter_initialized;
static FATFS log_fs;
static FIL log_file;
static bool log_mounted;
static bool log_open;
static uint32_t log_sample_count;

static const int16_t shunt_under_voltage = (int16_t)0x8000;
static const int16_t shunt_over_voltage = 0x7FFF;
static const int16_t bus_under_voltage = 0;
static const int16_t bus_over_voltage = 0x7FFF;
static const int16_t temperature_limit = 4836;
static const int16_t power_limit = 0x7FFF;

static uint8_t consume_u8_flag(volatile uint8_t *flag)
{
    uint8_t local;

    __disable_irq();
    local = *flag;
    *flag = 0;
    __enable_irq();

    return local;
}

static bool configure_ina229(ina229_t *ina, const char *name)
{
    if (ina229_write_configuration(ina, 0xBFF0) != INA229_OK) {
        uart_printf("INA229 %s config register failed\r\n", name);
        return false;
    }

    if (ina229_write_adc_configuration(ina, 0xFB6B) != INA229_OK) {
        uart_printf("INA229 %s adc config register failed\r\n", name);
        return false;
    }

    if (ina229_write_shunt_temperature_coefficient(ina, 0x005A) != INA229_OK) {
        uart_printf("INA229 %s shunt coefficient config failed\r\n", name);
        return false;
    }

    if (ina229_write_shunt_overvoltage_threshold(ina, shunt_over_voltage) != INA229_OK) {
        uart_printf("INA229 %s shunt overvoltage config failed\r\n", name);
        return false;
    }

    if (ina229_write_shunt_undervoltage_threshold(ina, shunt_under_voltage) != INA229_OK) {
        uart_printf("INA229 %s shunt undervoltage config failed\r\n", name);
        return false;
    }

    if (ina229_write_bus_overvoltage_threshold(ina, bus_over_voltage) != INA229_OK) {
        uart_printf("INA229 %s bus overvoltage config failed\r\n", name);
        return false;
    }

    if (ina229_write_bus_undervoltage_threshold(ina, bus_under_voltage) != INA229_OK) {
        uart_printf("INA229 %s bus undervoltage config failed\r\n", name);
        return false;
    }

    if (ina229_write_power_over_limit_threshold(ina, power_limit) != INA229_OK) {
        uart_printf("INA229 %s power limit config failed\r\n", name);
        return false;
    }

    if (ina229_write_temperature_over_limit_threshold(ina, temperature_limit) != INA229_OK) {
        uart_printf("INA229 %s temperature limit config failed\r\n", name);
        return false;
    }

    if (ina229_write_flags(ina, 0xE001) != INA229_OK) {
        uart_printf("INA229 %s flags config failed\r\n", name);
        return false;
    }

    return true;
}

static bool init_channel(ina229_t *ina, telemetry_channel_t *channel, const char *name)
{
    if (!configure_ina229(ina, name)) {
        return false;
    }

    if (ina229_init(ina) != INA229_OK) {
        uart_printf("INA229 %s init failed\r\n", name);
        return false;
    }

    if (ina229_read_manufacturer_id(ina, &channel->manufacturer_id) == INA229_OK) {
        uart_printf("%s Manufacturer ID: 0x%04X\r\n", name, channel->manufacturer_id);
    }

    if (ina229_read_device_id(ina, &channel->device_id) == INA229_OK) {
        uart_printf("%s Device ID: 0x%04X\r\n", name, channel->device_id);
    }

    ina229_read_flags(ina, &channel->diagnostics);
    delay_cycles(3200000U);

    return true;
}

static bool read_conversion(ina229_t *ina, telemetry_channel_t *channel)
{
    return (ina229_read_shunt_voltage(ina, &channel->shunt_voltage) == INA229_OK) &&
           (ina229_read_bus_voltage(ina, &channel->bus_voltage) == INA229_OK) &&
           (ina229_read_die_temperature(ina, &channel->die_temperature) == INA229_OK) &&
           (ina229_read_current(ina, &channel->current) == INA229_OK) &&
           (ina229_read_power(ina, &channel->power) == INA229_OK);
}

static bool telemetry_log_write(const char *text)
{
    UINT bytes_written;
    UINT length = (UINT)strlen(text);

    if (f_write(&log_file, text, length, &bytes_written) != FR_OK) {
        return false;
    }

    return bytes_written == length;
}

static bool telemetry_log_open(void)
{
    if (!log_mounted) {
        if (f_mount(&log_fs, "", 1) != FR_OK) {
            return false;
        }
        log_mounted = true;
    }

    if (!log_open) {
        if (f_open(&log_file, TELEMETRY_LOG_FILE, FA_OPEN_ALWAYS | FA_WRITE) != FR_OK) {
            return false;
        }

        if (f_lseek(&log_file, f_size(&log_file)) != FR_OK) {
            f_close(&log_file);
            return false;
        }

        log_open = true;
    }

    return true;
}

static void format_calendar_time(char *buffer,
                                 size_t buffer_size,
                                 uint16_t year,
                                 uint8_t month,
                                 uint8_t day,
                                 uint8_t hour,
                                 uint8_t minute,
                                 uint8_t second)
{
    snprintf(buffer,
             buffer_size,
             "%04u-%02u-%02uT%02u:%02u:%02u",
             year,
             month,
             day,
             hour,
             minute,
             second);
}

static void print_diagnostics(const char *name, uint16_t diagnostics)
{
    if (diagnostics & (1U << 7)) {
        uart_printf("%s temperature limit exceeded\r\n", name);
    }

    if (diagnostics & (1U << 6)) {
        uart_printf("%s shunt voltage upper limit exceeded\r\n", name);
    }

    if (diagnostics & (1U << 5)) {
        uart_printf("%s shunt voltage lower limit exceeded\r\n", name);
    }

    if (diagnostics & (1U << 4)) {
        uart_printf("%s bus voltage upper limit exceeded\r\n", name);
    }

    if (diagnostics & (1U << 3)) {
        uart_printf("%s bus voltage lower limit exceeded\r\n", name);
    }
}

bool telemetry_init(void)
{
    return init_channel(&ina_bat, &snapshot.battery, "Battery") &&
           init_channel(&ina_rect, &snapshot.rectifier, "Rectifier");
}

void telemetry_process_alerts(void)
{
    if (consume_u8_flag(&bat_flags)) {
        ina229_read_flags(&ina_bat, &snapshot.battery.diagnostics);
        print_diagnostics("Battery", snapshot.battery.diagnostics);

        if ((snapshot.battery.diagnostics & (1U << 1)) && !read_conversion(&ina_bat, &snapshot.battery)) {
            uart_printf("Battery read error\r\n");
        }
    }

    if (consume_u8_flag(&rect_flags)) {
        ina229_read_flags(&ina_rect, &snapshot.rectifier.diagnostics);
        print_diagnostics("Rectifier", snapshot.rectifier.diagnostics);

        if ((snapshot.rectifier.diagnostics & (1U << 1)) && !read_conversion(&ina_rect, &snapshot.rectifier)) {
            uart_printf("Rectifier read error\r\n");
        }
    }
}

bool telemetry_sample_battery_control(void)
{
    if (ina229_read_bus_voltage(&ina_bat, &snapshot.battery.bus_voltage) != INA229_OK) {
        uart_printf("Battery voltage read error\r\n");
        return false;
    }

    if (ina229_read_current(&ina_bat, &snapshot.battery.current) != INA229_OK) {
        uart_printf("Battery current read error\r\n");
        return false;
    }

    if (!vbat_filter_initialized) {
        snapshot.battery.filtered_bus_voltage = snapshot.battery.bus_voltage;
        vbat_filter_initialized = true;
    } else {
        snapshot.battery.filtered_bus_voltage +=
            VBAT_FILTER_ALPHA * (snapshot.battery.bus_voltage - snapshot.battery.filtered_bus_voltage);
    }

    return true;
}

bool telemetry_sample_power_supervisor(void)
{
    if (ina229_read_power(&ina_bat, &snapshot.battery.power) != INA229_OK) {
        uart_printf("Battery power read error\r\n");
        return false;
    }

    if (ina229_read_bus_voltage(&ina_rect, &snapshot.rectifier.bus_voltage) != INA229_OK) {
        uart_printf("Rectifier voltage read error\r\n");
        return false;
    }

    if (ina229_read_current(&ina_rect, &snapshot.rectifier.current) != INA229_OK) {
        uart_printf("Rectifier current read error\r\n");
        return false;
    }

    if (ina229_read_power(&ina_rect, &snapshot.rectifier.power) != INA229_OK) {
        uart_printf("Rectifier power read error\r\n");
        return false;
    }

    return true;
}

bool telemetry_log_header(void)
{
    if (!telemetry_log_open()) {
        return false;
    }

    if (f_size(&log_file) != 0U) {
        return true;
    }

    if (!telemetry_log_write(
            "sample,local_time,bat_v,bat_i,bat_p,rect_v,rect_i,rect_p,turbine_time,wind_m_s,rpm,turbine_state,turbine_crit,turbine_rx_ok\r\n")) {
        return false;
    }

    return f_sync(&log_file) == FR_OK;
}

bool telemetry_log_snapshot(void)
{
    char line[224];
    char local_time[24];
    char turbine_time[24];
    int length;

    if (!telemetry_log_header()) {
        return false;
    }

#if defined(RTC)
    DL_RTC_Common_Calendar calendar = DL_RTC_Common_getCalendarTime(RTC);
    format_calendar_time(local_time,
                         sizeof(local_time),
                         (uint16_t)calendar.year,
                         (uint8_t)calendar.month,
                         (uint8_t)calendar.dayOfMonth,
                         (uint8_t)calendar.hours,
                         (uint8_t)calendar.minutes,
                         (uint8_t)calendar.seconds);
#else
    local_time[0] = '\0';
#endif

    if (snapshot.turbine_timestamp_valid) {
        format_calendar_time(turbine_time,
                             sizeof(turbine_time),
                             snapshot.turbine_year,
                             snapshot.turbine_month,
                             snapshot.turbine_day,
                             snapshot.turbine_hour,
                             snapshot.turbine_minute,
                             snapshot.turbine_second);
    } else {
        turbine_time[0] = '\0';
    }

    length = snprintf(line,
                      sizeof(line),
                      "%lu,%s,%.3f,%.6f,%.3f,%.3f,%.6f,%.3f,%s,%.3f,%.3f,%u,%u,%u\r\n",
                      (unsigned long)log_sample_count++,
                      local_time,
                      snapshot.battery.filtered_bus_voltage,
                      snapshot.battery.current,
                      snapshot.battery.power,
                      snapshot.rectifier.bus_voltage,
                      snapshot.rectifier.current,
                      snapshot.rectifier.power,
                      turbine_time,
                      snapshot.turbine_wind_speed_m_s,
                      snapshot.turbine_rpm,
                      snapshot.turbine_state,
                      snapshot.turbine_critical_condition ? 1U : 0U,
                      snapshot.turbine_packet_valid ? 1U : 0U);

    if ((length <= 0) || ((size_t)length >= sizeof(line))) {
        return false;
    }

    if (!telemetry_log_write(line)) {
        return false;
    }

    return f_sync(&log_file) == FR_OK;
}

const telemetry_snapshot_t *telemetry_get_snapshot(void)
{
    return &snapshot;
}

void telemetry_update_turbine(float wind_speed_m_s,
                              float rpm,
                              uint8_t state,
                            //   bool critical_condition,
                            //   bool timestamp_valid,
                              uint16_t year,
                              uint8_t month,
                              uint8_t day,
                              uint8_t hour,
                              uint8_t minute,
                              uint8_t second)
{
    snapshot.turbine_wind_speed_m_s = wind_speed_m_s;
    snapshot.turbine_rpm = rpm;
    snapshot.turbine_state = state;
    // snapshot.turbine_critical_condition = critical_condition;
    // snapshot.turbine_timestamp_valid = timestamp_valid;
    snapshot.turbine_year = year;
    snapshot.turbine_month = month;
    snapshot.turbine_day = day;
    snapshot.turbine_hour = hour;
    snapshot.turbine_minute = minute;
    snapshot.turbine_second = second;
    snapshot.turbine_packet_valid = true;
}

void telemetry_set_battery_alert_flag(void)
{
    bat_flags = 1U;
}

void telemetry_set_rectifier_alert_flag(void)
{
    rect_flags = 1U;
}
