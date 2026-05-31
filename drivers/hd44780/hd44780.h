#ifndef HD44780_H_
#define HD44780_H_




#include "ti_msp_dl_config.h"
#include <stdint.h>




// =====================================================
// CONFIGURACIÓN
// =====================================================
// 1 = 4-bit mode
// 0 = 8-bit mode
#define LCD_4BIT_MODE  1




#define LCD_E_PULSE_DELAY_US   1
#define LCD_CMD_DELAY_US       50
#define LCD_CLEAR_DELAY_US     2000




// =====================================================
// API
// =====================================================
void hd44780_init(void);
void hd44780_send_instruction(uint8_t instruction);
void hd44780_print_char(uint8_t character);
void hd44780_print_string(const char *s);
void hd44780_clear_screen(void);
void hd44780_set_cursor(uint8_t x, uint8_t y);
void hd44780_print_u16(uint16_t n);




#endif
