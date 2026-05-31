#include <stdio.h>
#include <cstdlib>
#include <string.h>

#include "nvs_flash.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "driver/gpio.h"

#include "Wifi_Config.h"
#include "Wifi_Driver.h"
#include "Sonos.h"
#include "pinDefinitions.h"
#include "defines.h"
#include "MatrixDisplay.h"

static const char *TAG = "MAIN";

typedef struct
{
    char speaker_name[64];
} sonos_task_params_t;

void sonos_task(void *pvParameters)
{
    sonos_task_params_t *params = (sonos_task_params_t *)pvParameters;
    const char *speaker_name = params->speaker_name;

    ESP_LOGI(TAG, "Starting Sonos discovery for '%s'...", speaker_name);

    sonos_device_t *device = nullptr;
    const int max_retries = 3;
    const int retry_delay_ms = 2000;

    for (int attempt = 1; attempt <= max_retries; attempt++)
    {
        ESP_LOGI(TAG, "Sonos discovery attempt %d/%d", attempt, max_retries);
        device = sonos_find_device(speaker_name);

        if (device)
        {
            matrix_display_set_state(MATRIX_UI_PLAYBACK);
            break;
        }

        if (attempt < max_retries)
        {
            ESP_LOGW(TAG, "Device not found, retrying in %d ms...", retry_delay_ms);
            vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
        }
    }

    if (device)
    {
        ESP_LOGI(TAG, "Found Sonos device '%s' - %s",
                 device->deviceName,
                 device->Ipv4);

        sonos_start_notify();
        sonos_subscribe(device);
        free(device);
    }
    else
    {
        ESP_LOGW(TAG, "Device not found after %d attempts", max_retries);
    }

    free(params);
    vTaskDelete(NULL);
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Free heap at start: %lu", esp_get_free_heap_size());

    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Free heap after NVS: %lu", esp_get_free_heap_size());

    sonos_album_art_mutex = xSemaphoreCreateMutex();

    matrix_display_init();
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ENCODER_BTN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};

    gpio_config(&io_conf);

    vTaskDelay(pdMS_TO_TICKS(20));

    if (gpio_get_level(ENCODER_BTN) == 0)
    {
        ESP_LOGI(TAG, "Encoder button held at startup, entering WiFi config mode");
        wifi_config_start_ap(); // This component will block and requires a restart of the ESP to get by this stage
    }

    matrix_display_start_task();

    uint32_t boot_start_ms = esp_log_timestamp();

    char ssid[64];
    char pass[64];

    wifi_config_load(ssid, sizeof(ssid), pass, sizeof(pass));
    wifi_driver_init(ssid, pass);

    xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY);

    ESP_LOGI(TAG, "Free heap after WiFi: %lu", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Network ready, starting Sonos...");

    char sonos_speaker_name[64] = {0};
    if (!wifi_config_load_sonos_name(sonos_speaker_name, sizeof(sonos_speaker_name)))
    {
        strcpy(sonos_speaker_name, "Living Room");
        ESP_LOGW(TAG, "Failed to load Sonos speaker name, using default: %s", sonos_speaker_name);
    }

    uint32_t elapsed_boot_ms = esp_log_timestamp() - boot_start_ms;
    if (elapsed_boot_ms < 6000)
    {
        vTaskDelay(pdMS_TO_TICKS(6000 - elapsed_boot_ms));
    }

    sonos_task_params_t *params = (sonos_task_params_t *)malloc(sizeof(sonos_task_params_t));
    strncpy(params->speaker_name, sonos_speaker_name, sizeof(params->speaker_name) - 1);

    xTaskCreate(
        sonos_task,
        "sonos_task",
        8192,
        params,
        5,
        NULL);

    ESP_LOGI(TAG, "Free heap after task create: %lu", esp_get_free_heap_size());
}