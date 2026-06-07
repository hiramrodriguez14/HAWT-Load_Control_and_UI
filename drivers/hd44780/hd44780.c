#include "hd44780.h"

#if defined(LCD_RS_PORT) && defined(LCD_EN_PORT) && defined(LCD_D4_PORT) && \
    defined(LCD_D5_PORT) && defined(LCD_D6_PORT) && defined(LCD_D7_PORT)

// -----------------------------------------------------
// Delay helper
// -----------------------------------------------------
static inline void lcd_delay_us(uint32_t us)
{
   delay_cycles((CPUCLK_FREQ / 1000000UL) * us);
}


// -----------------------------------------------------
// Control pins
// -----------------------------------------------------
static inline void lcd_set_rs(uint8_t value)
{
   if (value) {
       DL_GPIO_setPins(LCD_RS_PORT, LCD_RS_PIN);
   } else {
       DL_GPIO_clearPins(LCD_RS_PORT, LCD_RS_PIN);
   }
}


static inline void lcd_set_en(uint8_t value)
{
   if (value) {
       DL_GPIO_setPins(LCD_EN_PORT, LCD_EN_PIN);
   } else {
       DL_GPIO_clearPins(LCD_EN_PORT, LCD_EN_PIN);
   }
}


static inline void lcd_pulse_en(void)
{
   lcd_set_en(1);
   lcd_delay_us(LCD_E_PULSE_DELAY_US);


   lcd_set_en(0);
   lcd_delay_us(LCD_E_PULSE_DELAY_US);
}




// -----------------------------------------------------
// 4-bit mode
// -----------------------------------------------------
#if LCD_4BIT_MODE




static inline void lcd_set_d4(uint8_t value)
{
   if (value) {
       DL_GPIO_setPins(LCD_D4_PORT, LCD_D4_PIN);
   } else {
       DL_GPIO_clearPins(LCD_D4_PORT, LCD_D4_PIN);
   }
}


static inline void lcd_set_d5(uint8_t value)
{
   if (value) {
       DL_GPIO_setPins(LCD_D5_PORT, LCD_D5_PIN);
   } else {
       DL_GPIO_clearPins(LCD_D5_PORT, LCD_D5_PIN);
   }
}


static inline void lcd_set_d6(uint8_t value)
{
   if (value) {
       DL_GPIO_setPins(LCD_D6_PORT, LCD_D6_PIN);
   } else {
       DL_GPIO_clearPins(LCD_D6_PORT, LCD_D6_PIN);
   }
}


static inline void lcd_set_d7(uint8_t value)
{
   if (value) {
       DL_GPIO_setPins(LCD_D7_PORT, LCD_D7_PIN);
   } else {
       DL_GPIO_clearPins(LCD_D7_PORT, LCD_D7_PIN);
   }
}


static inline void lcd_put_nibble(uint8_t nib)
{
   lcd_set_d4((nib >> 0) & 0x01);
   lcd_set_d5((nib >> 1) & 0x01);
   lcd_set_d6((nib >> 2) & 0x01);
   lcd_set_d7((nib >> 3) & 0x01);
}


static inline void lcd_write_byte(uint8_t byte, uint8_t rs)
{
   lcd_set_rs(rs);


   lcd_put_nibble((byte >> 4) & 0x0F);
   lcd_pulse_en();


   lcd_put_nibble(byte & 0x0F);
   lcd_pulse_en();


   lcd_delay_us(LCD_CMD_DELAY_US);
}


#else


// -----------------------------------------------------
// 8-bit mode
// -----------------------------------------------------
static inline void lcd_set_d0(uint8_t value)
{
   if (value) {
       DL_GPIO_setPins(LCD_D0_PORT, LCD_D0_PIN);
   } else {
       DL_GPIO_clearPins(LCD_D0_PORT, LCD_D0_PIN);
   }
}


static inline void lcd_set_d1(uint8_t value)
{
   if (value) {
       DL_GPIO_setPins(LCD_D1_PORT, LCD_D1_PIN);
   } else {
       DL_GPIO_clearPins(LCD_D1_PORT, LCD_D1_PIN);
   }
}


static inline void lcd_set_d2(uint8_t value)
{
   if (value) {
       DL_GPIO_setPins(LCD_D2_PORT, LCD_D2_PIN);
   } else {
       DL_GPIO_clearPins(LCD_D2_PORT, LCD_D2_PIN);
   }
}


static inline void lcd_set_d3(uint8_t value)
{
   if (value) {
       DL_GPIO_setPins(LCD_D3_PORT, LCD_D3_PIN);
   } else {
       DL_GPIO_clearPins(LCD_D3_PORT, LCD_D3_PIN);
   }
}


static inline void lcd_set_d4(uint8_t value)
{
   if (value) {
       DL_GPIO_setPins(LCD_D4_PORT, LCD_D4_PIN);
   } else {
       DL_GPIO_clearPins(LCD_D4_PORT, LCD_D4_PIN);
   }
}


static inline void lcd_set_d5(uint8_t value)
{
   if (value) {
       DL_GPIO_setPins(LCD_D5_PORT, LCD_D5_PIN);
   } else {
       DL_GPIO_clearPins(LCD_D5_PORT, LCD_D5_PIN);
   }
}


static inline void lcd_set_d6(uint8_t value)
{
   if (value) {
       DL_GPIO_setPins(LCD_D6_PORT, LCD_D6_PIN);
   } else {
       DL_GPIO_clearPins(LCD_D6_PORT, LCD_D6_PIN);
   }
}


static inline void lcd_set_d7(uint8_t value)
{
   if (value) {
       DL_GPIO_setPins(LCD_D7_PORT, LCD_D7_PIN);
   } else {
       DL_GPIO_clearPins(LCD_D7_PORT, LCD_D7_PIN);
   }
}


static inline void lcd_put_byte(uint8_t byte)
{
   lcd_set_d0((byte >> 0) & 0x01);
   lcd_set_d1((byte >> 1) & 0x01);
   lcd_set_d2((byte >> 2) & 0x01);
   lcd_set_d3((byte >> 3) & 0x01);
   lcd_set_d4((byte >> 4) & 0x01);
   lcd_set_d5((byte >> 5) & 0x01);
   lcd_set_d6((byte >> 6) & 0x01);
   lcd_set_d7((byte >> 7) & 0x01);
}


static inline void lcd_write_byte(uint8_t byte, uint8_t rs)
{
   lcd_set_rs(rs);
   lcd_put_byte(byte);
   lcd_pulse_en();
   lcd_delay_us(LCD_CMD_DELAY_US);
}


#endif


void hd44780_send_instruction(uint8_t instruction)
{
   lcd_write_byte(instruction, 0);
}


void hd44780_print_char(uint8_t character)
{
   lcd_write_byte(character, 1);
}


void hd44780_print_string(const char *s)
{
   while (*s) {
       hd44780_print_char((uint8_t)*s++);
   }
}


void hd44780_clear_screen(void)
{
   hd44780_send_instruction(0x01);
   lcd_delay_us(LCD_CLEAR_DELAY_US);
}


void hd44780_set_cursor(uint8_t x, uint8_t y)
{
   static const uint8_t row_offsets[4] = {0x00U, 0x40U, 0x14U, 0x54U};
   uint8_t addr;

   if (x >= 20U) {
      x = 19U;
   }

   if (y >= 4U) {
      y = 3U;
   }

   addr = (uint8_t)(row_offsets[y] + x);
   hd44780_send_instruction(0x80 | addr);
}


void hd44780_print_u16(uint16_t n)
{
   char buf[6];
   uint8_t i = 0;


   if (n == 0) {
       hd44780_print_char('0');
       return;
   }


   while ((n > 0) && (i < (sizeof(buf) - 1))) {
       buf[i++] = (char)('0' + (n % 10));
       n /= 10;
   }


   while (i > 0) {
       hd44780_print_char((uint8_t)buf[--i]);
   }
}


void hd44780_init(void)
{
   lcd_set_rs(0);
   lcd_set_en(0);


#if LCD_4BIT_MODE
   lcd_set_d4(0);
   lcd_set_d5(0);
   lcd_set_d6(0);
   lcd_set_d7(0);
#else
   lcd_set_d0(0);
   lcd_set_d1(0);
   lcd_set_d2(0);
   lcd_set_d3(0);
   lcd_set_d4(0);
   lcd_set_d5(0);
   lcd_set_d6(0);
   lcd_set_d7(0);
#endif


   lcd_delay_us(15000);


#if LCD_4BIT_MODE
   lcd_put_nibble(0x03);
   lcd_pulse_en();
   lcd_delay_us(5000);


   lcd_put_nibble(0x03);
   lcd_pulse_en();
   lcd_delay_us(200);




   lcd_put_nibble(0x03);
   lcd_pulse_en();
   lcd_delay_us(200);


   lcd_put_nibble(0x02);
   lcd_pulse_en();
   lcd_delay_us(200);


   hd44780_send_instruction(0x28);
   hd44780_send_instruction(0x0C);
   hd44780_send_instruction(0x06);
   hd44780_clear_screen();
#else
   lcd_write_byte(0x38, 0);
   lcd_delay_us(5000);


   lcd_write_byte(0x38, 0);
   lcd_delay_us(200);


   lcd_write_byte(0x38, 0);
   lcd_delay_us(200);


   hd44780_send_instruction(0x38);
   hd44780_send_instruction(0x0C);
   hd44780_send_instruction(0x06);
   hd44780_clear_screen();
#endif
}

#else

void hd44780_init(void)
{
}

void hd44780_send_instruction(uint8_t instruction)
{
   (void)instruction;
}

void hd44780_print_char(uint8_t character)
{
   (void)character;
}

void hd44780_print_string(const char *s)
{
   (void)s;
}

void hd44780_clear_screen(void)
{
}

void hd44780_set_cursor(uint8_t x, uint8_t y)
{
   (void)x;
   (void)y;
}

void hd44780_print_u16(uint16_t n)
{
   (void)n;
}

#endif
