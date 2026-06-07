#include "ti_msp_dl_config.h"
#include <stdbool.h>
#include "turbine_uart.h"

#define UNHANDLED_INTERRUPT()                                                  \
  do {                                                                         \
    __disable_irq();                                                           \
    __BKPT(0);                                                                 \
    while (1) {                                                                \
    }                                                                          \
  } while (0)






const char *turbine_state_string_lut[] = {
    [TURBINE_STATE_INIT] = "STATE_INIT",       [TURBINE_STATE_MAXIMIZE] = "STATE_MAXIMIZE",
    [TURBINE_STATE_RATED] = "STATE_RATED",     [TURBINE_STATE_DURABILITY] = "STATE_DURABILITY",
    [TURBINE_STATE_RESTART] = "STATE_RESTART", [TURBINE_STATE_SAFETY] = "STATE_SAFETY",
};



typedef enum {
  RX_WAIT_FOR_SOF,
  RX_LENGTH,
  RX_PADDING,
  RX_PAYLOAD,
} RxState;

volatile RxState global_rx_state = RX_WAIT_FOR_SOF;

volatile uint8_t global_rx_length;
volatile uint32_t global_rx_payload_index;

TelemetryPacket global_rx_packet;
volatile bool global_rx_packet_ready = false;

void UART_TURBINE_INST_IRQHandler(void) {
  switch (DL_UART_getPendingInterrupt(UART_TURBINE_INST)) {
  case DL_UART_IIDX_RX: {
    uint8_t byte = DL_UART_Main_receiveData(UART_TURBINE_INST);
    switch (global_rx_state) {
    case RX_WAIT_FOR_SOF:
      if (byte == 0xAA) {
        global_rx_state = RX_LENGTH;
      }
      break;
    case RX_LENGTH:
      global_rx_length = byte;

      if (global_rx_length != sizeof(TelemetryPacket)) {
        global_rx_state = RX_WAIT_FOR_SOF;
      } else {
        global_rx_state = RX_PADDING;
      }
      break;
    case RX_PADDING:
      global_rx_payload_index = 0;
      global_rx_state = RX_PAYLOAD;
      break;
    case RX_PAYLOAD:
      ((uint8_t *)&global_rx_packet)[global_rx_payload_index++] = byte;

      if (global_rx_payload_index >= global_rx_length) {
        global_rx_packet_ready = true;
        global_rx_state = RX_WAIT_FOR_SOF;
      }
      break;
    }
    break;
  }
  default:
    UNHANDLED_INTERRUPT();
    break;
  }
}

void uart_turbine_send_condition(bool critical) {
  uint8_t condition = 0;
  condition |= critical << 0;
  DL_UART_transmitData(UART_TURBINE_INST, (uint8_t)condition);
}
