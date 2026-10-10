/*
 * lcd.c
 *
 *  Ported from the STM32 HAL version (Parth, 09-Apr-2026) to ESP-IDF.
 *  Logic is unchanged; only the platform calls differ.
 */

#include "lcd.h"
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "esp_log.h"

static const char *TAG = "LCD";

//Private Macros
#define LCD_BACKLIGHT			0x08		//P3 bit
#define LCD_NO_BACKLIGHT		0x00

#define LCD_EN					0x04		//P2 bit - Enable
#define LCD_RW					0x02		//P1 bit - Read/Write (always 0)
#define LCD_RS					0x01		//P0 bit - Register Select

//Commands
#define LCD_CLEAR_DISPLAY		0x01
#define LCD_RETURN_HOME			0x02
#define LCD_ENTRY_MODE			0x06
#define LCD_DISPLAY_ON			0x0C		//Display ON, Cursor OFF, Blink OFF
#define LCD_FUNCTION_SET		0x28		//4 bit, 2 lines, 5x8 font
#define LCD_SET_DDRAM			0x80		//Base Address for DDRAM

//Row Offset
#define LCD_ROW0_OFFSET			0x00
#define LCD_ROW1_OFFSET			0x40

//Enable-pulse hold time. The original used HAL_Delay(1) (1 ms). The LCD only needs
//~1 us here and each I2C write already takes ~200 us, so 50 us is plenty.
//If characters ever come out garbled, raise this.
#define LCD_PULSE_US			50

//Private Variables
static i2c_master_dev_handle_t _dev = NULL;
static uint8_t _backlight = LCD_BACKLIGHT;	//On by default
static bool _present = false;				//false = display missing/failed, all writes are skipped

//Private Function Prototypes
static void LCD_SendByte(uint8_t byte, uint8_t rs);
static void LCD_SendNibble(uint8_t nibble);
static void LCD_I2C_Write(uint8_t data);
static void	LCD_SendCommand(uint8_t cmd);
static void LCD_SendData(uint8_t data);

// HAL_Delay() replacement. FreeRTOS ticks are 10 ms by default in ESP-IDF, so
// vTaskDelay(pdMS_TO_TICKS(2)) rounds down to 0 ticks and doesn't wait at all.
// Short waits are busy-waited instead; only long ones sleep the task.
static void lcd_delay_ms(uint32_t ms){
	if(ms >= 20){
		vTaskDelay(pdMS_TO_TICKS(ms));
	}else{
		esp_rom_delay_us(ms * 1000);
	}
}

//Public Functions
LCD_Status_t LCD_Init(i2c_master_bus_handle_t bus){
	//Allow LCD_Init() to be called again to recover after a failure
	if(_dev != NULL){
		i2c_master_bus_rm_device(_dev);
		_dev = NULL;
	}
	_present = false;

	//Check the display is actually there before touching it. A missing LCD must
	//never take down gas monitoring.
	if(i2c_master_probe(bus, LCD_I2C_ADDR, 50) != ESP_OK){
		ESP_LOGW(TAG, "No device at I2C address 0x%02X (try 0x3F, check wiring)", LCD_I2C_ADDR);
		return LCD_ERROR;
	}

	//ESP-IDF takes the plain 7-bit address. The STM32 HAL needed it shifted left
	//by 1 (LCD_I2C_ADDR << 1); do NOT shift here.
	i2c_device_config_t dev_cfg = {
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = LCD_I2C_ADDR,
		.scl_speed_hz = 100000,
	};
	if(i2c_master_bus_add_device(bus, &dev_cfg, &_dev) != ESP_OK){
		ESP_LOGW(TAG, "Failed to add LCD to the I2C bus");
		return LCD_ERROR;
	}
	_present = true;

	lcd_delay_ms(100);

	//1) Init sequence - force 4 bit mode
	LCD_SendNibble(0x03);
	lcd_delay_ms(10);
	LCD_SendNibble(0x03);
	lcd_delay_ms(5);
	LCD_SendNibble(0x03);
	lcd_delay_ms(2);

	//2) Switch to 4 bit mode
	LCD_SendNibble(0x02);
	lcd_delay_ms(2);

	LCD_SendCommand(LCD_FUNCTION_SET); 	//Configure: 4 Bit, 2 Lines, 5x8 Font
	lcd_delay_ms(2);

	LCD_SendCommand(LCD_DISPLAY_ON);	//Display ON, Cursor OFF
	lcd_delay_ms(2);

	LCD_SendCommand(LCD_CLEAR_DISPLAY);	//Clear Display
	lcd_delay_ms(5);					//Clear needs more time

	LCD_SendCommand(LCD_ENTRY_MODE);	//Move cursor to right
	lcd_delay_ms(2);

	return _present ? LCD_OK : LCD_ERROR;
}

void LCD_Clear(void){
	LCD_SendCommand(LCD_CLEAR_DISPLAY);
	lcd_delay_ms(2);
}

void LCD_SetCursor(uint8_t row, uint8_t col){
	static const uint8_t offsets[] = {LCD_ROW0_OFFSET, LCD_ROW1_OFFSET};

	//Bug fix: the STM32 version used '>' here, so row == LCD_ROWS (2) slipped
	//through and read past the end of offsets[]. Valid indexes are 0..LCD_ROWS-1.
	if(row >= LCD_ROWS) row = LCD_ROWS - 1;
	if(col >= LCD_COLS) col = LCD_COLS - 1;

	LCD_SendCommand(LCD_SET_DDRAM | (offsets[row] + col));
}

void LCD_Print(const char *str){
	while(*str){
		LCD_PrintChar(*str++);
	}
}

void LCD_PrintChar(char ch){
	LCD_SendData((uint8_t)ch);
}

void LCD_Backlight(uint8_t on){
	_backlight = on ? LCD_BACKLIGHT : LCD_NO_BACKLIGHT;
	LCD_I2C_Write(0x00);		//Send dummy data to apply backlight setting immediately
}


//Private Functions
static void LCD_I2C_Write(uint8_t data){
	if(!_present) return;

	data |= _backlight;
	//If the display disappears mid-run, stop talking to it instead of stalling the
	//sensor loop on repeated I2C timeouts. Call LCD_Init() again to bring it back.
	if(i2c_master_transmit(_dev, &data, 1, 50) != ESP_OK){
		ESP_LOGW(TAG, "I2C write failed - LCD disabled until LCD_Init() is called again");
		_present = false;
	}
}

static void LCD_SendNibble(uint8_t nibble){
	uint8_t data = (nibble << 4) & 0xF0;

	LCD_I2C_Write(data | LCD_EN);		//EN High
	esp_rom_delay_us(LCD_PULSE_US);
	LCD_I2C_Write(data & ~(LCD_EN));	//EN Low
	esp_rom_delay_us(LCD_PULSE_US);
}

static void LCD_SendByte(uint8_t byte, uint8_t rs){
	uint8_t rs_bit = rs ? LCD_RS : 0x00;

	uint8_t high = (byte & 0xF0) >> 4;
	uint8_t low = (byte & 0x0F);

	//Send High
	uint8_t data = ((high << 4) & 0XF0) | rs_bit;
	LCD_I2C_Write(data | LCD_EN);
	esp_rom_delay_us(LCD_PULSE_US);
	LCD_I2C_Write(data & ~(LCD_EN));
	esp_rom_delay_us(LCD_PULSE_US);

	//Send Low
	data = ((low << 4) & 0xF0) | rs_bit;
	LCD_I2C_Write(data | LCD_EN);
	esp_rom_delay_us(LCD_PULSE_US);
	LCD_I2C_Write(data & ~(LCD_EN));
	esp_rom_delay_us(LCD_PULSE_US);
}

static void LCD_SendCommand(uint8_t cmd){
	LCD_SendByte(cmd, 0);		//RS = 0 for Command
}

static void LCD_SendData(uint8_t data){
	LCD_SendByte(data, 1);		//RS = 1 for Data
}