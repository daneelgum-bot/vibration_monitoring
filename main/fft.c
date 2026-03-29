#include "esp_dsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "param.h"
#include "esp_log.h"
#include "adxl345.h"
#include "buffers.h"
#include "math.h"

#define HP_CUTOFF 10.0f
//#define HP_Q 0.7071f
#define HP_Q1 0.5412f
#define HP_Q2 1.3066f
#define G_TO_MMS2 9819.3f
#define SAMPLING_RATE 3200.0f

static float window[BUF_SIZE];
//static float hp_coeffs[5];
static float hp_coeffs1[5];
static float hp_coeffs2[5];
static float filter_state1[2] = {0, 0};
static float filter_state2[2] = {0, 0};
static float *fft_input = NULL;

void fft_task(void *pvParameters)
{
    fft_input = heap_caps_malloc(BUF_SIZE * 2 * sizeof(float), MALLOC_CAP_8BIT);
    if (!fft_input)
    {
        ESP_LOGE("FFT", "  malloc failed");
        vTaskDelete(NULL);
        return;
    }

    dsps_fft2r_init_fc32(NULL, BUF_SIZE);
    dsps_wind_hann_f32(window, BUF_SIZE);

    float norm_freq = HP_CUTOFF / (SAMPLING_RATE / 2.0f);
    //esp_err_t res = dsps_biquad_gen_hpf_f32(hp_coeffs, norm_freq, HP_Q);
    
    esp_err_t res1 = dsps_biquad_gen_hpf_f32(hp_coeffs1, norm_freq, HP_Q1);
    esp_err_t res2 = dsps_biquad_gen_hpf_f32(hp_coeffs2, norm_freq, HP_Q2);
    
    
    if (res1 != ESP_OK || res2 != ESP_OK)
    {
        ESP_LOGE("FFT", "Failed to generate filter coefficients");
    }
    else
    {
        ESP_LOGI("FFT", "HPF generated, fc=%.1f Hz", HP_CUTOFF);
    }
    adxl345_buffer_t *buf;

    while (1)
    {
        if (xQueueReceive(s_data_queue, &buf, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }
        if (buf == NULL)
        {
            ESP_LOGE("FFT", "Received NULL buffer");
            continue;
        }

        
       dsps_biquad_f32(buf->accel, buf->accel, BUF_SIZE, hp_coeffs1, filter_state1);
       dsps_biquad_f32(buf->accel, buf->accel, BUF_SIZE, hp_coeffs2, filter_state2);
        
        for (int i = 0; i < BUF_SIZE; i++)
        {
            buf->accel[i] = buf->accel[i] * G_TO_MMS2;
            fft_input[i * 2] = buf->accel[i] * window[i]; // действительная часть (*Окно Ханна)
            fft_input[i * 2 + 1] = 0.0f;
        }

        dsps_fft2r_fc32(fft_input, BUF_SIZE); // БПФ
        dsps_bit_rev_fc32(fft_input, BUF_SIZE);

       // float speed_spectrum[BUF_SIZE / 2] = {0};
        float rms_speed = 0.0f;
        for (int i = 1; i < BUF_SIZE / 2; i++)
        {
            float freq = i * SAMPLING_RATE / BUF_SIZE;
            float a_real = fft_input[i * 2];
            float a_imag = fft_input[i * 2 + 1];
            float abs = (2.0f/BUF_SIZE)*sqrtf(a_real * a_real + a_imag * a_imag);
            buf->spectrum[i] = abs / (2.0f * M_PI * freq)*2;
            rms_speed +=  buf->spectrum[i] *  buf->spectrum[i];
        }
        buf->spectrum[0]=0.0f;
        buf->rms_speed = sqrtf(rms_speed * 0.5f);
        
        buf->sensor_id=1;

        if (xQueueSend(s_spectrum_queue, &buf, 0) != pdTRUE)
        {
            ESP_LOGE("FFT", "Failed to return buffer to free_queue");
        }
    }
}
