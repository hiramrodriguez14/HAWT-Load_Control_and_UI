#include "telemetry.h"

#include "ti_msp_dl_config.h"
#include "drivers/ina229/ina229.h"
#include "drivers/uart_debug.h"

#define VBAT_FILTER_ALPHA 0.1f

static ina229_t ina_bat = {
    .spi_inst = SPI_0_INST,
    .cs_port = (uint32_t)BATTERY_CS_PORT,
    .cs_pin = BATTERY_CS_CS1_PIN,
    .r_shunt_ohms = 0.05f,
    .current_lsb = 6.25e-6f,
    .adc_range = 0
};

static ina229_t ina_rect = {
    .spi_inst = SPI_1_INST,
    .cs_port = (uint32_t)RECT_CS_PORT,
    .cs_pin = RECT_CS_CS2_PIN,
    .r_shunt_ohms = 0.05f,
    .current_lsb = 6.25e-6f,
    .adc_range = 0
};

static telemetry_snapshot_t snapshot;
static volatile uint8_t bat_flags;
static volatile uint8_t rect_flags;
static bool vbat_filter_initialized;

static const int16_t shunt_under_voltage = (int16_t)0x8000;
static const int16_t shunt_over_voltage = 0x7FFF;
static const int16_t bus_under_voltage = 0;
static const int16_t bus_over_voltage = 0x7FFF;
static const int16_t temperature_limit = 4000;
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
    return false;
}

bool telemetry_log_snapshot(void)
{
    return false;
}

const telemetry_snapshot_t *telemetry_get_snapshot(void)
{
    return &snapshot;
}

void telemetry_update_turbine(float wind_speed_m_s,
                              float rpm,
                              uint8_t state,
                              bool critical_condition)
{
    snapshot.turbine_wind_speed_m_s = wind_speed_m_s;
    snapshot.turbine_rpm = rpm;
    snapshot.turbine_state = state;
    snapshot.turbine_critical_condition = critical_condition;
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
