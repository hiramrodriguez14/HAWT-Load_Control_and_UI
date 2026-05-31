#ifndef TELEMETRY_H_
#define TELEMETRY_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t manufacturer_id;
    uint16_t device_id;
    float shunt_voltage;
    float bus_voltage;
    float filtered_bus_voltage;
    float die_temperature;
    float current;
    float power;
    float energy;
    float charge;
    uint16_t diagnostics;
} telemetry_channel_t;

typedef struct {
    telemetry_channel_t battery;
    telemetry_channel_t rectifier;
    float turbine_wind_speed_m_s;
    float turbine_rpm;
    uint8_t turbine_state;
    bool turbine_critical_condition;
    bool turbine_packet_valid;
} telemetry_snapshot_t;

bool telemetry_init(void);
void telemetry_process_alerts(void);
bool telemetry_sample_battery_control(void);
bool telemetry_sample_power_supervisor(void);
bool telemetry_log_header(void);
bool telemetry_log_snapshot(void);
const telemetry_snapshot_t *telemetry_get_snapshot(void);
void telemetry_update_turbine(float wind_speed_m_s,
                              float rpm,
                              uint8_t state,
                              bool critical_condition);
void telemetry_set_battery_alert_flag(void);
void telemetry_set_rectifier_alert_flag(void);

#endif /* TELEMETRY_H_ */
