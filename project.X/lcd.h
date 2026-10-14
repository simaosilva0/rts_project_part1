#ifndef LCD_H
#define LCD_H

#include <stdint.h>

#define LCD_I2C_ADDR 0x27   /* 7-bit. 0x3F if PCF8574AT */

void lcd_init(void);
void lcd_clear(void);
void lcd_goto(uint8_t row, uint8_t col);   /* row 0-1, col 0-15 */
void lcd_putc(char c);
void lcd_puts(const char *s);
void lcd_put2(uint8_t v);                  /* 2 digits, zero padded */
void lcd_cursor(uint8_t on, uint8_t blink);

#endif//meowmoew moew moeeew