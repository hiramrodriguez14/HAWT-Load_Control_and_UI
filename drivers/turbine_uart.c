#include "turbine_uart.h"

#include "ti_msp_dl_config.h"

#include <string.h>

typedef enum {
    PARSER_WAIT_SYNC0,
    PARSER_WAIT_SYNC1,
    PARSER_VERSION,
    PARSER_LENGTH,
    PARSER_PAYLOAD,
    PARSER_CHECKSUM
} parser_state_t;

static parser_state_t parser_state;
static uint8_t payload[TURBINE_UART_PAYLOAD_LENGTH];
static uint8_t payload_index;
static uint8_t checksum;

static volatile uint8_t sample_ready;
static turbine_uart_sample_t latest_sample;
static uint32_t packet_count;
static uint32_t error_count;

static float read_float_le(const uint8_t *bytes)
{
    float value;
    uint8_t raw[sizeof(value)];

    raw[0] = bytes[0];
    raw[1] = bytes[1];
    raw[2] = bytes[2];
    raw[3] = bytes[3];
    memcpy(&value, raw, sizeof(value));

    return value;
}

static void send_byte(uint8_t byte)
{
#if defined(TURBINE_UART_INST)
    while (DL_UART_Main_isBusy(TURBINE_UART_INST)) {
    }

    DL_UART_Main_transmitData(TURBINE_UART_INST, byte);
#else
    (void)byte;
#endif
}

static void parser_reset(void)
{
    parser_state = PARSER_WAIT_SYNC0;
    payload_index = 0U;
    checksum = 0U;
}

static void accept_payload(void)
{
    latest_sample.rpm = read_float_le(&payload[0]);
    latest_sample.wind_speed_m_s = read_float_le(&payload[4]);
    latest_sample.state = payload[8];
    latest_sample.critical_condition = (payload[9] != 0U);
    sample_ready = 1U;
    packet_count++;
}

void turbine_uart_init(void)
{
    parser_reset();

#if defined(TURBINE_UART_INST)
    DL_UART_Main_enableInterrupt(TURBINE_UART_INST, DL_UART_MAIN_INTERRUPT_RX);
#endif
}

void turbine_uart_on_rx_byte(uint8_t byte)
{
    switch (parser_state) {
        case PARSER_WAIT_SYNC0:
            if (byte == TURBINE_UART_SYNC0) {
                parser_state = PARSER_WAIT_SYNC1;
            }
            break;

        case PARSER_WAIT_SYNC1:
            if (byte == TURBINE_UART_SYNC1) {
                parser_state = PARSER_VERSION;
                checksum = 0U;
            } else if (byte != TURBINE_UART_SYNC0) {
                parser_reset();
            }
            break;

        case PARSER_VERSION:
            checksum ^= byte;
            if (byte == TURBINE_UART_PROTOCOL_VERSION) {
                parser_state = PARSER_LENGTH;
            } else {
                error_count++;
                parser_reset();
            }
            break;

        case PARSER_LENGTH:
            checksum ^= byte;
            if (byte == TURBINE_UART_PAYLOAD_LENGTH) {
                payload_index = 0U;
                parser_state = PARSER_PAYLOAD;
            } else {
                error_count++;
                parser_reset();
            }
            break;

        case PARSER_PAYLOAD:
            payload[payload_index++] = byte;
            checksum ^= byte;

            if (payload_index >= TURBINE_UART_PAYLOAD_LENGTH) {
                parser_state = PARSER_CHECKSUM;
            }
            break;

        case PARSER_CHECKSUM:
            if (byte == checksum) {
                accept_payload();
            } else {
                error_count++;
            }
            parser_reset();
            break;

        default:
            parser_reset();
            break;
    }
}

void turbine_uart_send_control(uint8_t local_state, bool critical_condition)
{
    uint8_t checksum = 0U;
    uint8_t critical = critical_condition ? 1U : 0U;

    send_byte(TURBINE_UART_SYNC0);
    send_byte(TURBINE_UART_SYNC1);

    send_byte(TURBINE_UART_PROTOCOL_VERSION);
    checksum ^= TURBINE_UART_PROTOCOL_VERSION;

    send_byte(TURBINE_UART_CONTROL_LENGTH);
    checksum ^= TURBINE_UART_CONTROL_LENGTH;

    send_byte(local_state);
    checksum ^= local_state;

    send_byte(critical);
    checksum ^= critical;

    send_byte(checksum);
}

bool turbine_uart_get_latest(turbine_uart_sample_t *sample)
{
    if ((sample == 0) || !sample_ready) {
        return false;
    }

    __disable_irq();
    *sample = latest_sample;
    sample_ready = 0U;
    __enable_irq();

    return true;
}

uint32_t turbine_uart_get_packet_count(void)
{
    return packet_count;
}

uint32_t turbine_uart_get_error_count(void)
{
    return error_count;
}
