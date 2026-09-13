#include "DHT11.h"
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

#define DHT11_PIN   GPIO_NUM_27

/************** Private Function Prototypes **************/
static void DHT11_SetOutput(void);
static void DHT11_SetInput(void);

/************** Public Functions **************/
void DHT11_Init(void){
	gpio_reset_pin(DHT11_PIN);
	gpio_set_direction(DHT11_PIN, GPIO_MODE_OUTPUT_OD);
	gpio_set_level(DHT11_PIN, 1);
}

void DHT11_Start(void){
	DHT11_SetOutput();
	gpio_set_level(DHT11_PIN, 0);
	esp_rom_delay_us(18000);
	DHT11_SetInput();
	esp_rom_delay_us(40);
}

DHT11_Status_t DHT11_CheckResponse(void){
    uint32_t timeout = 0;

    //1) Wait for DHT to pull line low (error if takes longer than 100us)
	timeout = 0;
	while(gpio_get_level(DHT11_PIN)){
		esp_rom_delay_us(1);
		timeout++;
		if(timeout > 100){
			return DHT11_ERROR_NO_RESPONSE;
		}
	}

    //2) Check if pulls high after 80us (100us to check for error)
	timeout = 0;
	while(!gpio_get_level(DHT11_PIN)){
		esp_rom_delay_us(1);
		timeout++;
		if(timeout > 100){
			return DHT11_ERROR_TIMEOUT_LOW;
		}
	}

    //3) Check if pulls low again after 80us (100us to check for error)
	timeout = 0;
	while(gpio_get_level(DHT11_PIN)){
		esp_rom_delay_us(1);
		timeout++;
		if(timeout > 100){
			return DHT11_ERROR_TIMEOUT_HIGH;
		}
	}

    return DHT11_OK;
}

DHT11_Status_t DHT11_ReadBit(uint8_t *bit){

    uint32_t timeout = 0;
    int64_t start_us;
    uint32_t pulse_width;

    /* Wait for HIGH */
    timeout = 0;
    while (!gpio_get_level(DHT11_PIN)) {
        esp_rom_delay_us(1);
        if (++timeout > 100) {
            return DHT11_BIT_TIMEOUT_ERROR;
        }
    }

    /* Measure HIGH pulse width. */
    start_us = esp_timer_get_time();

        while (gpio_get_level(DHT11_PIN)) {
        if ((esp_timer_get_time() - start_us) > 100) {
            return DHT11_BIT_TIMEOUT_ERROR;
        }
    }

    pulse_width = (uint32_t)(esp_timer_get_time() - start_us);

    /*Decode Bit*/
    *bit = (pulse_width > 50) ? 1 : 0;

    /* Wait for post-bit LOW (separator) */
    timeout = 0;
    while (gpio_get_level(DHT11_PIN)) {
        esp_rom_delay_us(1);
        if (++timeout > 100) {
            return DHT11_BIT_TIMEOUT_ERROR;
        }
    }

    return DHT11_OK;
}

DHT11_Status_t DHT11_ReadByte(uint8_t *byte){
    uint8_t bit = 0;

    *byte = 0;

    for(uint8_t i = 0; i<8; i++){
        
        DHT11_Status_t Read_Bit_Status = DHT11_ReadBit(&bit);
        
        if(Read_Bit_Status != DHT11_OK){
            return Read_Bit_Status;
        }

        if(bit == 0){
            *byte &= ~(1 << (7-i));
        }

        if(bit == 1){
            *byte |= (1 << (7-i));
        }
    }

    return DHT11_OK;
}

DHT11_Status_t DHT11_ReadData(DHT11_Data_t *data){
 
	uint8_t RawData[5];
	uint8_t byte = 0;
	uint8_t checksum;

    //1) Send Start Signal
	DHT11_Start();

	//2.) Check Response
	DHT11_Status_t response = DHT11_CheckResponse();

	if(response != DHT11_OK){
		return response;
	}

	//3) Read 5 bytes of data and store
	for(uint8_t i = 0; i < 5; i++){
 
		DHT11_Status_t ByteDataResponse = DHT11_ReadByte(&byte);
 
		if(ByteDataResponse != DHT11_OK){
			return ByteDataResponse;
		}
 
		RawData[i] = byte;
		byte = 0;
	}

	//4) Calculate Checksum
	checksum = RawData[0] + RawData[1] + RawData[2] + RawData[3];
	checksum &= 0xFF;

	//5) Validate Checksum
	if(checksum != RawData[4]){
		return DHT11_CHECKSUM_ERROR;
	}

    data->humidity = RawData[0];
	data->temperature = RawData[2];

    return DHT11_OK;

}

/************** Private Helper Functions **************/
static void DHT11_SetOutput(void){
    gpio_set_direction(DHT11_PIN, GPIO_MODE_OUTPUT_OD);
}
 
static void DHT11_SetInput(void){
    gpio_set_direction(DHT11_PIN, GPIO_MODE_INPUT);
}