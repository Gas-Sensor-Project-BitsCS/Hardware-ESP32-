#ifndef DHT11_H_
#define DHT11_H_
 
#include <stdint.h>
 
typedef enum{
	DHT11_OK = 0,
	DHT11_ERROR_NO_RESPONSE,
	DHT11_ERROR_TIMEOUT_LOW,
	DHT11_ERROR_TIMEOUT_HIGH,
	DHT11_BIT_TIMEOUT_ERROR,
	DHT11_CHECKSUM_ERROR
} DHT11_Status_t;
 
typedef struct{
	uint8_t temperature;
	uint8_t humidity;
} DHT11_Data_t;
 
void DHT11_Init(void);
void DHT11_Start(void);
DHT11_Status_t DHT11_ReadData(DHT11_Data_t *data);
DHT11_Status_t DHT11_CheckResponse(void);
DHT11_Status_t DHT11_ReadBit(uint8_t *bit);
DHT11_Status_t DHT11_ReadByte(uint8_t *byte);

#endif