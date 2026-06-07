#include "uart_debug.h"

#if UART_DEBUG_ENABLE

#include "ti_msp_dl_config.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

void uart_send_char(char c)
{
    while (DL_UART_Main_isBusy(UART_0_INST)) {
    }

    DL_UART_Main_transmitData(UART_0_INST, (uint8_t)c);
}

void uart_send_string(const char *s)
{
    while (*s) {
        uart_send_char(*s++);
    }
}

void uart_printf(const char *fmt, ...)
{
    char buffer[512];
    va_list args;
    int len;

    va_start(args, fmt);
    len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (len < 0) {
        return;
    }

    if (len >= (int)sizeof(buffer)) {
        len = sizeof(buffer) - 1;
    }

    for (int i = 0; i < len; i++) {
        uart_send_char(buffer[i]);
    }
}

#endif
