#include <stdio.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"

#include "wifi_connect.h"
#include "http_client.h"

static const char *TAG = "MQ2";

/* -------------------- Configuration -------------------- */
//GPIO34 = ADC1_CHANNEL_6
#define MQ2_ADC_UNIT        ADC_UNIT_1
#define MQ2_ADC_CHANNEL     ADC_CHANNEL_6
#define MQ2_ADC_ATTEN       ADC_ATTEN_DB_12     //0-3.3V full range

// We built a 10k/10k voltage divider so GPIO34 only sees half of the
// MQ-2's actual AOUT voltage. Multiply readings by this to reconstruct
// the real AOUT value.
#define DIVIDER_FACTOR          2.0f
#define VCC                     5.0f    // Sensor is powered at 5V
#define RL_VALUE_KOHM           5.0f    //Sensors on board load resistor (assume 5k ohm)

#define RO_CLEAN_AIR_FACTOR     9.83f   //MQ-2 datasheet clean air factor Rs/Ro ration

#define WARMUP_MS               60000   //Warmup the sensor once at start to let the sensor become stable (reduced to 6s for quick testing)
#define CALIBRATION_SAMPLES     50      //Number of times to check reading and avg it in clean air for a baseline

// Power-law regression constants per gas: ppm = a * (Rs/Ro)^b
// Source: MQ-2 datasheet log-log curve fits (MQSensorsLib constants).
// No distinct "Smoke" curve is commonly published for MQ-2 - it's
// approximated here with the LPG curve, since the two lines sit close
// together on the datasheet graph. Treat as an estimate, not exact.
#define SMOKE_A                 574.25f
#define SMOKE_B                 -2.222f

// Dummy placeholders until the temperature/humidity sensor is wired up.
#define DUMMY_TEMPERATURE_C      30.0f
#define DUMMY_HUMIDITY_PCT       60.0f

/* -------------------- Alert config (Phase 2, hardcoded) -------------------- */
#define LED_GPIO                 GPIO_NUM_25
#define BUZZER_GPIO              GPIO_NUM_26

#define GAS_THRESHOLD_PPM_ON      25.0f   // trigger alert above this
#define GAS_THRESHOLD_PPM_OFF     20.0f   // clear alert below this (hysteresis)


/* -------------------- Globals -------------------- */
static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t cali_handle = NULL;
static bool have_calibration = false;
static float Ro = 10.0f; //Overwritten by calibrate_ro()

static bool alert_active = false;

static void update_alert(float smoke_ppm){
    if (!alert_active && smoke_ppm >= GAS_THRESHOLD_PPM_ON) {
        alert_active = true;
        ESP_LOGW(TAG, "ALERT: smoke_ppm=%.1f crossed ON threshold (%.1f)", smoke_ppm, GAS_THRESHOLD_PPM_ON);
    } else if (alert_active && smoke_ppm <= GAS_THRESHOLD_PPM_OFF) {
        alert_active = false;
        ESP_LOGI(TAG, "Alert cleared: smoke_ppm=%.1f dropped below OFF threshold (%.1f)", smoke_ppm, GAS_THRESHOLD_PPM_OFF);
    }
    // else: stay in whatever state we were already in (this is the hysteresis band)

    gpio_set_level(LED_GPIO, alert_active ? 1 : 0);
    gpio_set_level(BUZZER_GPIO, alert_active ? 1 : 0);
}

/* -------------------- ADC Setup -------------------- */
static void adc_init(void){

    // Creating ADC Unit Handle
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = MQ2_ADC_UNIT
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc_handle));

    // Configure the specific channel we are reading
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = MQ2_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, MQ2_ADC_CHANNEL, &chan_cfg));

    // Set up calibration so raw counts convert to accurate millivolts,
    // correcting for this specific chip's factory-measured characteristics.
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = MQ2_ADC_UNIT,
        .atten = MQ2_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    have_calibration = (adc_cali_create_scheme_line_fitting(&cali_cfg, &cali_handle) == ESP_OK);

    if (!have_calibration){
        // Falls back to simple linear math further down if this chip/IDF
        // combo doesn't support the calibration scheme.
        ESP_LOGW(TAG, "ADC Calibration scheme unavailable, using raw linear math");
    }
}

/* -------------------- Reading + math-------------------- */
// Returns the ACTUAL AOUT voltage at the sensor, after undoing the voltage divider.
static float read_aout_voltage(void){
    int raw = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, MQ2_ADC_CHANNEL, &raw));

    int mv_at_gpio = 0;

    if(have_calibration){
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, raw, &mv_at_gpio));
    }else{
        mv_at_gpio = (int)((raw / 4095.0f) * 3300.0f);
    }

    float v_at_gpio = mv_at_gpio / 1000.0f; //Voltage at GPIO34
    float v_aout = v_at_gpio * DIVIDER_FACTOR; //real AOUT, before the divider voltage

    return v_aout;
}

//Rs = ((VCC - Vout) / Vout) * RL
static float compute_rs(float v_out){
    if(v_out <= 0.01f) v_out = 0.01f; // guard agaisnt divide by zero
    return ((VCC - v_out) / v_out) * RL_VALUE_KOHM;
}

//Generic power-law converter, reused for every gas: ppm = a * ratio^b
static float compute_ppm(float ratio, float a, float b){
    if(ratio <= 0.0f) ratio = 0.01f;
    return a * powf(ratio, b);
}

static void calibrate_ro(void){
    ESP_LOGI(TAG, "Calibrating Ro in clean air (%d samples)", CALIBRATION_SAMPLES);
    float rs_sum = 0.0f;

    for(int i = 0; i < CALIBRATION_SAMPLES; i++){
        rs_sum += compute_rs(read_aout_voltage());
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    float rs_avg = rs_sum / CALIBRATION_SAMPLES;
    Ro = rs_avg / RO_CLEAN_AIR_FACTOR;
    ESP_LOGI(TAG, "Calibration done. Ro = %.2f kOhm", Ro);
}

/* -------------------- GPIO(Buzzer + LED) Setup -------------------- */

static void alert_gpio_init(void){
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO) | (1ULL << BUZZER_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Start in a known "off" state
    gpio_set_level(LED_GPIO, 0);
    gpio_set_level(BUZZER_GPIO, 0);
}

/* -------------------- Main -------------------- */

void app_main(void)
{
    adc_init();
    alert_gpio_init();

    // Connect to Wi-Fi before we do anything that needs the network.
    // This blocks until connected (or gives up after a few retries).
    if (wifi_connect_init() != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi connection failed, sensor readings will not be sent");
    }

    ESP_LOGI(TAG, "Warming up (%d s)... keep sensor in clean air for calibration.", WARMUP_MS / 1000);
    vTaskDelay(pdMS_TO_TICKS(WARMUP_MS));

    calibrate_ro();

    while(1){
        float v_aout = read_aout_voltage();
        float rs = compute_rs(v_aout);
        float ratio = rs / Ro;

        float smoke_ppm = compute_ppm(ratio, SMOKE_A, SMOKE_B);

        update_alert(smoke_ppm);

        printf("\nAOUT:%.3fV Rs:%.2fk Ro:%.2fk Rs/Ro:%.2f | Smoke:%.1f ppm\n", v_aout, rs, Ro, ratio, smoke_ppm);

        // Send this reading to the backend. Temperature/humidity are dummy
        // values until that sensor is wired up.
        esp_err_t send_err = send_sensor_reading(smoke_ppm, DUMMY_TEMPERATURE_C, DUMMY_HUMIDITY_PCT);
        if (send_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to send reading to server");
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
