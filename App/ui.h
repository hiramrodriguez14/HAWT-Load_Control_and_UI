#ifndef UI_H_
#define UI_H_

#include <stdint.h>

/*
 * Expected SysConfig names for full 20x4 UI support:
 *   LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7
 *   UI_BUTTON_NEXT, UI_BUTTON_PREV, UI_BUTTON_START_STOP, UI_BUTTON_RECORD
 *   UI_LED_RED, UI_LED_GREEN
 *
 * Configure SPST buttons as active-low with pull-up and rise/fall interrupts.
 */

void ui_init(void);
void ui_task(void);
void ui_update(void);
void ui_handle_gpio_interrupt(uint32_t gpioa_pending, uint32_t gpiob_pending);
unsigned char ui_is_system_running(void);
unsigned char ui_is_record_enabled(void);

#endif /* UI_H_ */
