/*
 * lcd.h
 *
 * HD44780 16x2 LCD over a PCF8574 I2C backpack - ESP-IDF port of the
 * STM32 HAL driver. Same public API; only the I2C handle type changed.
 */

#ifndef LCD_H
#define LCD_H

#include <stdint.h>
#include "driver/i2c_master.h"

#define LCD_I2C_ADDR    0x27    // 7-bit address. If init fails, try 0x3F (PCF8574A backpacks)
#define LCD_ROWS        2
#define LCD_COLS        16

typedef enum {
    LCD_OK = 0,
    LCD_ERROR
} LCD_Status_t;

// Takes the bus handle created by i2c_new_master_bus() in app_main.
// Returns LCD_ERROR (without crashing) if no display answers at LCD_I2C_ADDR.
LCD_Status_t LCD_Init(i2c_master_bus_handle_t bus);

void LCD_Clear(void);
void LCD_SetCursor(uint8_t row, uint8_t col);
void LCD_Print(const char *str);
void LCD_PrintChar(char ch);
void LCD_Backlight(uint8_t on);

#endif /* LCD_H */