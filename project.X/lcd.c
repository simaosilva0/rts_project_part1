#include "lcd.h"
#include "mcc_generated_files/mcc.h"
#include "mcc_generated_files/examples/i2c1_master_example.h"

/* PCF8574 pins: P0=RS P1=RW P2=E P3=BL P4-P7=D4-D7 */
#define LCD_RS 0x01
#define LCD_E  0x04
#define LCD_BL 0x08

static uint8_t buf[4];
static uint8_t disp_ctl = 0x0C;   /* display on, cursor off, blink off */

/* Blocking (polled MCC driver, no timeout). Main loop only, never from an ISR. */
static void i2c_write(uint8_t len)
{
    I2C1_WriteNBytes(LCD_I2C_ADDR, buf, len);
}

/* Single nibble (init only) */
static void write_nibble(uint8_t nib)
{
    uint8_t d = (uint8_t)((nib << 4) | LCD_BL);
    buf[0] = d | LCD_E;
    buf[1] = d;
    i2c_write(2);
}

static void write_byte(uint8_t val, uint8_t rs)
{
    uint8_t hi = (uint8_t)((val & 0xF0) | LCD_BL | rs);
    uint8_t lo = (uint8_t)(((val << 4) & 0xF0) | LCD_BL | rs);
    buf[0] = hi | LCD_E;
    buf[1] = hi;
    buf[2] = lo | LCD_E;
    buf[3] = lo;
    i2c_write(4);
}

static void cmd(uint8_t c)
{
    write_byte(c, 0);
}

void lcd_init(void)
{
    __delay_ms(50);
    write_nibble(0x3);
    __delay_ms(5);
    write_nibble(0x3);
    __delay_us(200);
    write_nibble(0x3);
    __delay_us(200);
    write_nibble(0x2);          /* 4-bit mode */
    __delay_us(200);

    cmd(0x28);                  /* 4-bit, 2 lines, 5x8 */
    cmd(0x08);                  /* display off */
    cmd(0x01);                  /* clear */
    __delay_ms(2);
    cmd(0x06);                  /* entry mode: increment, no shift */
    disp_ctl = 0x0C;
    cmd(disp_ctl);
}

void lcd_clear(void)
{
    cmd(0x01);
    __delay_ms(2);
}

void lcd_goto(uint8_t row, uint8_t col)
{
    cmd((uint8_t)(0x80 | (row ? 0x40 : 0x00) | (col & 0x0F)));
}

void lcd_putc(char c)
{
    write_byte((uint8_t)c, LCD_RS);
}

void lcd_puts(const char *s)
{
    while (*s) {
        lcd_putc(*s++);
    }
}

void lcd_put2(uint8_t v)
{
    lcd_putc((char)('0' + (v / 10) % 10));
    lcd_putc((char)('0' + v % 10));
}

void lcd_cursor(uint8_t on, uint8_t blink)
{
    disp_ctl = (uint8_t)(0x0C | (on ? 0x02 : 0) | (blink ? 0x01 : 0));
    cmd(disp_ctl);
}
