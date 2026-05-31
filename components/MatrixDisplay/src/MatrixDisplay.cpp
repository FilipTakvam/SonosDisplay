#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"
#include "pinDefinitions.h"
#include "defines.h"
#include "Sonos.h"
#include "esp_log.h"

MatrixPanel_I2S_DMA *dma_display = nullptr;

enum AnimationState
{
    IDLE,
    SLIDE_OUT_OLD,
    PAUSE_ANIM,
    SLIDE_IN_NEW
};

static AnimationState anim_state = IDLE;
static uint32_t anim_start_time = 0;
static const uint32_t anim_duration_ms = 900;
static const uint32_t black_pause_ms = 500;

static uint8_t prev_album_art[64][64][3] = {};
static uint8_t next_album_art[64][64][3] = {};

static bool has_previous = false;

static volatile int brightness = 30;
static volatile int last_clk_state = 0;

static void IRAM_ATTR encoder_isr(void *arg)
{
    int clk = gpio_get_level(ENCODER_CLK);
    int dt = gpio_get_level(ENCODER_DT);

    if (clk != last_clk_state)
    {
        if (dt != clk)
            brightness += 1;
        else
            brightness -= 1;

        if (brightness > 255)
            brightness = 255;
        if (brightness < 5)
            brightness = 5;

        last_clk_state = clk;
    }
}

static void init_encoder()
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ENCODER_CLK) | (1ULL << ENCODER_DT) | (1ULL << ENCODER_BTN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&io_conf);

    last_clk_state = gpio_get_level(ENCODER_CLK);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(ENCODER_CLK, encoder_isr, NULL);
}

static inline float ease_in_out(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

static inline float get_t(uint32_t start, uint32_t duration_ms)
{
    uint32_t elapsed = esp_log_timestamp() - start;
    float t = (float)elapsed / duration_ms;
    return (t > 1.0f) ? 1.0f : ease_in_out(t);
}

static bool album_art_changed(uint8_t a[64][64][3], uint8_t b[64][64][3])
{
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++)
            for (int c = 0; c < 3; c++)
                if (a[y][x][c] != b[y][x][c])
                    return true;

    return false;
}

static void draw_composite(MatrixPanel_I2S_DMA *display,
                           uint8_t img_a[64][64][3], int offset_a,
                           uint8_t img_b[64][64][3], int offset_b)
{
    for (int screen_y = 0; screen_y < DISPLAY_HEIGHT; screen_y++)
    {
        for (int screen_x = 0; screen_x < DISPLAY_WIDTH; screen_x++)
        {
            int src_y_a = screen_y - offset_a;
            int src_y_b = screen_y - offset_b;

            uint16_t color = 0;

            if (src_y_a >= 0 && src_y_a < DISPLAY_HEIGHT)
                color = display->color565(
                    img_a[src_y_a][screen_x][0],
                    img_a[src_y_a][screen_x][1],
                    img_a[src_y_a][screen_x][2]);

            if (src_y_b >= 0 && src_y_b < DISPLAY_HEIGHT)
                color = display->color565(
                    img_b[src_y_b][screen_x][0],
                    img_b[src_y_b][screen_x][1],
                    img_b[src_y_b][screen_x][2]);
            
            display->drawPixel(screen_x, screen_y, color);
        }
    }
}

static void display_task(void *pvParameters)
{
    MatrixPanel_I2S_DMA *display = (MatrixPanel_I2S_DMA *)pvParameters;

    static uint8_t current_art[64][64][3];
    int applied_brightness = brightness;

    while (true)
    {
        if (applied_brightness != brightness)
        {
            applied_brightness = brightness;
            display->setBrightness8(applied_brightness);
        }

        if (anim_state == IDLE)
        {
            xSemaphoreTake(sonos_album_art_mutex, portMAX_DELAY);
            memcpy(current_art, sonos_get_album_art_64(), sizeof(current_art));
            xSemaphoreGive(sonos_album_art_mutex);

            if (!has_previous)
            {
                memcpy(prev_album_art, current_art, sizeof(prev_album_art));
                has_previous = true;
            }
            else if (album_art_changed(prev_album_art, current_art))
            {
                memcpy(next_album_art, current_art, sizeof(next_album_art));
                anim_state = SLIDE_OUT_OLD;
                anim_start_time = esp_log_timestamp();
            }

            draw_composite(display,
                           prev_album_art, 0,
                           prev_album_art, DISPLAY_HEIGHT);
        }

        else if (anim_state == SLIDE_OUT_OLD)
        {
            float t = get_t(anim_start_time, anim_duration_ms);
            int offset = (int)(t * -DISPLAY_HEIGHT);

            draw_composite(display,
                           prev_album_art, offset,
                           next_album_art, DISPLAY_HEIGHT);

            if (t >= 1.0f)
            {
                anim_state = PAUSE_ANIM;
                anim_start_time = esp_log_timestamp();
            }
        }

        else if (anim_state == PAUSE_ANIM)
        {
            draw_composite(display,
                           prev_album_art, DISPLAY_HEIGHT,
                           next_album_art, DISPLAY_HEIGHT);

            if ((esp_log_timestamp() - anim_start_time) >= black_pause_ms)
            {
                anim_state = SLIDE_IN_NEW;
                anim_start_time = esp_log_timestamp();
            }
        }

        else if (anim_state == SLIDE_IN_NEW)
        {
            float t = get_t(anim_start_time, anim_duration_ms);
            int offset = (int)((1.0f - t) * DISPLAY_HEIGHT);

            draw_composite(display,
                           prev_album_art, DISPLAY_HEIGHT,
                           next_album_art, offset);

            if (t >= 1.0f)
            {
                memcpy(prev_album_art, next_album_art, sizeof(prev_album_art));
                anim_state = IDLE;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(16));
    }
}

#ifdef __cplusplus
extern "C" {
#endif

void matrix_display_init()
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
    mxconfig.clkphase = false;

    dma_display = new MatrixPanel_I2S_DMA(mxconfig);

    if (!dma_display->begin())
    {
        return;
    }

    dma_display->setBrightness8(brightness);
    dma_display->clearScreen();

    init_encoder();
}

void matrix_display_start_task()
{
    xTaskCreatePinnedToCore(
        display_task,
        "display_task",
        8192,
        dma_display,
        5,
        NULL,
        1);
}

void matrix_display_set_brightness(int new_brightness)
{
    brightness = new_brightness;
}

int matrix_display_get_brightness()
{
    return brightness;
}

#ifdef __cplusplus
}
#endif
