#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "MQ2";

/* -------------------- Configuration -------------------- */
//GPIO34 = ADC1_CHANNEL_6
#define MQ2_ADC_UNIT        ADC_UNIT_1
#define MQ2_ADC_CHANNEL     ADC_CHANNEL_6
#define MQ2_ADC_ATTEN       ADC_ATTEN_DB_12     //0-3.3V full range

// We built a 10k/10k voltage divider so GPIO34 only sees half of the
// MQ-2's actual AOUT voltage. Multiply readings by this to reconstruct
// the real AOUT value.
#define DIVIDER_FACTOR    2.0f

/* -------------------- Globals -------------------- */
static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t cali_handle = NULL;
static bool have_calibration = false;

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

/* -------------------- Reading -------------------- */
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

/* -------------------- Main -------------------- */

void app_main(void)
{
    adc_init();
    ESP_LOGI(TAG, "ADC + calibration initialized, starting voltage reads...");

    while(1){
        float v_aout = read_aout_voltage();
        printf("AOUT Voltage: %.3f V\n", v_aout);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
