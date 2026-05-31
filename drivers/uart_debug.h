#ifndef UART_DEBUG_H_
#define UART_DEBUG_H_

#ifndef UART_DEBUG_ENABLE
#define UART_DEBUG_ENABLE 1
#endif

#if UART_DEBUG_ENABLE

void uart_send_char(char c);
void uart_send_string(const char *s);
void uart_printf(const char *fmt, ...);

#else

#define uart_send_char(c)        ((void)0)
#define uart_send_string(s)      ((void)0)
#define uart_printf(...)         ((void)0)

#endif

#endif /* UART_DEBUG_H_ */
