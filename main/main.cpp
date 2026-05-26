#include <stdio.h>
#include <cstdlib>

#include "nvs_flash.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "Wifi_Driver.h"
#include "Sonos.h"

#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"

static const char *TAG = "MAIN";

// DISPLAY - Refactor later - just for testing

#define DISPLAY_WIDTH 64
#define DISPLAY_HEIGHT 64
MatrixPanel_I2S_DMA *dma_display = nullptr;
uint8_t frameBuffer[DISPLAY_HEIGHT][DISPLAY_WIDTH][3];

void display_task(void *pvParameters)
{
    MatrixPanel_I2S_DMA *display = (MatrixPanel_I2S_DMA *)pvParameters;

    while (true)
    {
        uint8_t (*album_art)[64][3] = sonos_get_album_art_64();

        for (int y = 0; y < DISPLAY_HEIGHT; y++)
        {
            for (int x = 0; x < DISPLAY_WIDTH; x++)
            {
                uint8_t r = album_art[y][x][0];
                uint8_t g = album_art[y][x][1];
                uint8_t b = album_art[y][x][2];
                uint16_t rgb565 = display->color565(r, g, b);
                display->drawPixel(x, y, rgb565);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(16));
    }
}

#define R1_PIN 25
#define G1_PIN 26
#define B1_PIN 27
#define R2_PIN 14
#define G2_PIN 12
#define B2_PIN 13
#define A_PIN 23
#define B_PIN 22
#define C_PIN 5
#define D_PIN 17
#define E_PIN 32
#define LAT_PIN 4
#define OE_PIN 15
#define CLK_PIN 16

void init_display()
{

    HUB75_I2S_CFG::i2s_pins _pins = {
        R1_PIN, G1_PIN, B1_PIN,
        R2_PIN, G2_PIN, B2_PIN,
        A_PIN, B_PIN, C_PIN, D_PIN, E_PIN,
        LAT_PIN, OE_PIN, CLK_PIN};

    HUB75_I2S_CFG mxconfig(
        DISPLAY_WIDTH,
        DISPLAY_HEIGHT,
        1,
        _pins,
        HUB75_I2S_CFG::FM6126A);

    dma_display = new MatrixPanel_I2S_DMA(mxconfig);

    if (!dma_display->begin())
    {
        ESP_LOGE("TAG", "Initialization of display failed");
        return;
    }

    dma_display->setBrightness8(80);
    dma_display->clearScreen();

    ESP_LOGI(TAG, "Display initialized");
}

void sonos_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting Sonos discovery...");

    sonos_device_t *device = sonos_find_device("Kök");

    if (device)
    {
        ESP_LOGI(TAG, "Found Sonos device '%s' - %s", device->deviceName, device->Ipv4);
        sonos_start_notify();
        ESP_LOGI(TAG, "Subscribing to device at %s", device->Ipv4);
        sonos_subscribe(device);
        free(device);
    }
    else
    {
        ESP_LOGW(TAG, "Device not found");
    }

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

    init_display();

    xTaskCreatePinnedToCore(
        display_task,
        "display_task",
        8192,
        dma_display, // <- pvParameters
        5,
        NULL,
        1 // Core 1
    );

    wifi_driver_init();

    // Wait until Wi-Fi is fully connected (GOT_IP)
    xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY);

    ESP_LOGI(TAG, "Free heap after WiFi: %lu", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Network ready, starting Sonos...");

    xTaskCreate(
        sonos_task,
        "sonos_task",
        8192,
        NULL,
        5,
        NULL);

    ESP_LOGI(TAG, "Free heap after task create: %lu", esp_get_free_heap_size());
}