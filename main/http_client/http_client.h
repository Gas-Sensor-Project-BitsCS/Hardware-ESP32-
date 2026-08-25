#ifndef APP_HTTP_CLIENT_H
#define APP_HTTP_CLIENT_H
 
#include "esp_err.h"
 
/**
 * POSTs a sensor reading to the backend as JSON:
 *   { "gas_value": ..., "temperature": ..., "humidity": ... }
 *
 * Returns ESP_OK if the HTTP request completed (check logs for status code),
 * or an error code if the request itself failed (e.g. no connection).
 */
esp_err_t send_sensor_reading(float gas_value, float temperature, float humidity);
 
#endif // APP_HTTP_CLIENT_H