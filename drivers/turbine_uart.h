#ifndef TURBINE_UART_H_
#define TURBINE_UART_H_

#include <stdbool.h>
#include <stdint.h>

#include "ti_msp_dl_config.h"

typedef enum {
  TURBINE_STATE_INIT,
  TURBINE_STATE_MAXIMIZE,
  TURBINE_STATE_RATED,
  TURBINE_STATE_DURABILITY,
  TURBINE_STATE_RESTART,
  TURBINE_STATE_SAFETY,
} TurbineState;

// #include <stdbool.h>
// #include <stdint.h>

// #define TURBINE_UART_SYNC0              0xA5U
// #define TURBINE_UART_SYNC1              0x5AU
// #define TURBINE_UART_PROTOCOL_VERSION   0x02U
// #define TURBINE_UART_PAYLOAD_LENGTH     17U
// #define TURBINE_UART_CONTROL_LENGTH     2U

// /*
//  * Wire format, all little-endian:
//  *   telemetry: sync0 sync1 version payload_len rpm[4] wind_speed_m_s[4] state critical
//  *       year[2] month day hour minute second checksum
//  *   control: sync0 sync1 version payload_len local_state critical checksum
//  * checksum = XOR(version, payload_len, payload bytes)
//  */

// typedef enum {
//     TURBINE_STATE_INIT = 0,
//     TURBINE_STATE_MAXIMIZE,
//     TURBINE_STATE_RATED,
//     TURBINE_STATE_DURABILITY,
//     TURBINE_STATE_RESTART,
//     TURBINE_STATE_SAFETY
// } turbine_remote_state_t;

// typedef struct {
//     float rpm;
//     float wind_speed_m_s;
//     uint8_t state;
//     bool critical_condition;
//     bool timestamp_valid;
//     uint16_t year;
//     uint8_t month;
//     uint8_t day;
//     uint8_t hour;
//     uint8_t minute;
//     uint8_t second;
// } turbine_uart_sample_t;

// void turbine_uart_init(void);
// void turbine_uart_on_rx_byte(uint8_t byte);
// void turbine_uart_send_control(uint8_t local_state, bool critical_condition);
// bool turbine_uart_get_latest(turbine_uart_sample_t *sample);
// uint32_t turbine_uart_get_packet_count(void);
// uint32_t turbine_uart_get_error_count(void);


typedef struct __attribute__((packed)) {
  DL_RTC_Common_Calendar calendar;
  float hall_effect_rpm, wind_speed_m_s, blade_deg;
  TurbineState state;
} TelemetryPacket;
typedef struct __attribute__((packed)) {
  uint8_t start_of_frame; // 0xAA
  uint8_t length;         // sizeof(payload)
  uint8_t _padding[1];
  TelemetryPacket payload;
  // uint32_t crc;
} Frame;

extern TelemetryPacket global_rx_packet;
extern volatile bool global_rx_packet_ready;

void uart_turbine_send_condition(bool critical);

#endif /* TURBINE_UART_H_ */
