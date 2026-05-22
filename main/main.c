#include <stdio.h>

#include "nvs_flash.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "Wifi_Driver.h"
#include "Sonos.h"

static const char *TAG = "MAIN";

void sonos_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting Sonos discovery...");

    sonos_device_t *devices = NULL;
    int count = 0;

    sonos_discovery_start(&devices, &count);

    ESP_LOGI(TAG, "Found %d Sonos device(s)", count);

    free(devices);

    vTaskDelete(NULL);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    wifi_driver_init();

    // Wait until Wi-Fi is fully connected (GOT_IP)
    xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY);

    ESP_LOGI(TAG, "Network ready, starting Sonos...");

    xTaskCreate(
        sonos_task,
        "sonos_task",
        8192,
        NULL,
        5,
        NULL);
}