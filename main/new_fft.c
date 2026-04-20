#include "esp_dsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "new_buffers.h"
#include <math.h>

static const char *TAG = "FFT";

#define HP_CUTOFF 10.0f              // частота среза ФВЧ (Гц)
#define HP_Q1 0.5412f                // добротность первого каскада
#define HP_Q2 1.3066f                // добротность второго каскада
#define G_TO_MMS2 9819.3f            // перевод g -> мм/с²
#define SAMPLING_RATE (1 / 0.000313) // частота дискретизации

static float window[BUF_SIZE];
static float hp_coeffs1[5];
static float hp_coeffs2[5];
static float filter_state1_sensor1[2] = {0};
static float filter_state2_sensor1[2] = {0};
static float filter_state1_sensor2[2] = {0};
static float filter_state2_sensor2[2] = {0};
static float *fft_input = NULL;

void fft_task(void *pvParameters)
{
    fft_input = heap_caps_malloc(BUF_SIZE * 2 * sizeof(float), MALLOC_CAP_8BIT);
    assert(fft_input);

    dsps_fft2r_init_fc32(NULL, BUF_SIZE);
    dsps_wind_hann_f32(window, BUF_SIZE);

    float norm_freq = HP_CUTOFF / (SAMPLING_RATE / 2.0f);
    dsps_biquad_gen_hpf_f32(hp_coeffs1, norm_freq, HP_Q1);
    dsps_biquad_gen_hpf_f32(hp_coeffs2, norm_freq, HP_Q2);

    ESP_LOGI(TAG, "FFT task started");

    adxl345_buffer_t *buf;

    while (1)
    {
        if (xQueueReceive(s_data_queue, &buf, portMAX_DELAY) != pdTRUE || buf == NULL)
        {
            continue;
        }

        //остояния фильтра sensor_id
        float *state1, *state2;
        int sensor_id = (int)buf->sensor_id;
        if (sensor_id == 1)
        {
            state1 = filter_state1_sensor1;
            state2 = filter_state2_sensor1;
        }
        else
        {
            state1 = filter_state1_sensor2;
            state2 = filter_state2_sensor2;
        }

        dsps_biquad_f32(buf->accel, buf->accel, BUF_SIZE, hp_coeffs1, state1);
        dsps_biquad_f32(buf->accel, buf->accel, BUF_SIZE, hp_coeffs2, state2);

        for (int i = 0; i < BUF_SIZE; i++)
        {
            float val_mmss = buf->accel[i] * G_TO_MMS2;
            fft_input[i * 2] = val_mmss * window[i]; // реальная часть
            fft_input[i * 2 + 1] = 0.0f;             // мнимая часть
        }

        dsps_fft2r_fc32(fft_input, BUF_SIZE);
        dsps_bit_rev_fc32(fft_input, BUF_SIZE);

        float rms_speed_sq = 0.0f;
        //buf->spectrum[0] = 0.0f;
        int min_i = (int)ceilf(HP_CUTOFF * BUF_SIZE / SAMPLING_RATE);
        for (int i = 0; i < min_i; i++)
        {
            buf->spectrum[i] = 0.0f;
        }
        
        for (int i = min_i; i < BUF_SIZE / 2; i++)
        {
            float freq = i * SAMPLING_RATE / BUF_SIZE;
            float real = fft_input[i * 2];
            float imag = fft_input[i * 2 + 1];
            float amp = (2.0f / BUF_SIZE) * sqrtf(real * real + imag * imag) * 2; // амплитуда ускорения (мм/с²)

            // Интегрирование ускорения -> скорость: V = A / (2πf)
            buf->spectrum[i] = amp / (2.0f * M_PI * freq);
            rms_speed_sq += buf->spectrum[i] * buf->spectrum[i];
        }

        buf->rms_speed = sqrtf(rms_speed_sq * 0.5f); // СКЗ скорости (мм/с)

        if (xQueueSend(s_spectrum_queue, &buf, 0) != pdTRUE)
        {
            ESP_LOGW(TAG, "Spectrum queue full, returning buffer to free pool");
            xQueueSend(s_free_queue, &buf, 0);
        }
    }
}
