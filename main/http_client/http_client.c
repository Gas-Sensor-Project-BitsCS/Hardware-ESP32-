#include <string.h>
#include <stdlib.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"

#include "http_client.h"

static const char *TAG = "HTTP_CLIENT";

/* Server URL now comes from Kconfig (CONFIG_SENSOR_SERVER_URL) instead of
 * being hardcoded here. Set it via `idf.py menuconfig` under
 * "Mining Project Configuration" -> "Sensor readings server URL".
 *
 * IMPORTANT: it must be your PC's actual LAN IP, not 127.0.0.1 --
 * 127.0.0.1 refers to "this device" from the ESP32's own point of view,
 * not your PC. Find your PC's IP with `ipconfig` (Windows) or
 * `ifconfig` / `ip a` (Mac/Linux). The ESP32 and PC must be on the same
 * Wi-Fi network. */
#define SERVER_URL CONFIG_SENSOR_SERVER_URL

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t send_sensor_reading(float gas_value, float temperature, float humidity)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "gas_value", gas_value);
    cJSON_AddNumberToObject(root, "temperature", temperature);
    cJSON_AddNumberToObject(root, "humidity", humidity);

    char *post_data = cJSON_PrintUnformatted(root);
    if (post_data == NULL) {
        ESP_LOGE(TAG, "Failed to serialize JSON body");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    esp_http_client_config_t config = {
        .url = SERVER_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "POST %s -> HTTP status %d", SERVER_URL, status);
    } else {
        ESP_LOGE(TAG, "POST failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    cJSON_Delete(root);
    free(post_data);

    return err;
}