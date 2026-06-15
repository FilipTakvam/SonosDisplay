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
#include "MatrixDisplay.h"

#include "TuneFrame.h"
#include "WifiFault.h"

MatrixPanel_I2S_DMA *dma_display = nullptr;

static AnimationState anim_state = IDLE;
static uint32_t anim_start_time = 0;
static const uint32_t anim_duration_ms = 900;
static const uint32_t black_pause_ms = 500;

static BootAnimState boot_anim_state = BOOT_FADE_IN;
static uint32_t boot_anim_start = 0;

static const uint32_t boot_fade_in_ms = 600;
static const uint32_t boot_hold_ms = 5000;
static const uint32_t boot_fade_out_ms = 600;

static MatrixUiState ui_state = MATRIX_UI_BOOT;

static uint8_t prev_album_art[64][64][3] = {};
static uint8_t next_album_art[64][64][3] = {};

static bool has_previous = false;

static volatile int brightness = 100;
static volatile int last_clk_state = 0;

static const int SNAKE_PERIMETER = (DISPLAY_WIDTH + DISPLAY_HEIGHT - 2) * 2; // 252

static const uint32_t SNAKE_STEP_MS   = 6;    // ms per pixel advance
static const uint32_t SNAKE_PAUSE_MS  = 400;  // pause between fill and erase, and after erase

typedef enum
{
    SNAKE_FILL,
    SNAKE_PAUSE_AFTER_FILL,
    SNAKE_ERASE,
    SNAKE_PAUSE_AFTER_ERASE,
} SnakePhase;

static SnakePhase snake_phase      = SNAKE_FILL;
static int        snake_head       = 0;
static int        snake_tail       = 0;
static uint32_t   snake_last_step  = 0;
static uint32_t   snake_pause_start = 0;
static bool       snake_bg_drawn   = false;

static void snake_perimeter_to_xy(int idx, int *x, int *y)
{
    // Top row: y=0, x=0..W-1
    if (idx < DISPLAY_WIDTH)
    {
        *x = idx;
        *y = 0;
        return;
    }
    idx -= DISPLAY_WIDTH;

    // Right column: x=W-1, y=1..H-1
    if (idx < DISPLAY_HEIGHT - 1)
    {
        *x = DISPLAY_WIDTH - 1;
        *y = idx + 1;
        return;
    }
    idx -= (DISPLAY_HEIGHT - 1);

    // Bottom row: y=H-1, x=W-2..0
    if (idx < DISPLAY_WIDTH - 1)
    {
        *x = DISPLAY_WIDTH - 2 - idx;
        *y = DISPLAY_HEIGHT - 1;
        return;
    }
    idx -= (DISPLAY_WIDTH - 1);

    // Left column: x=0, y=H-2..1
    *x = 0;
    *y = DISPLAY_HEIGHT - 2 - idx;
}

static void snake_reset()
{
    snake_phase       = SNAKE_FILL;
    snake_head        = 0;
    snake_tail        = 0;
    snake_last_step   = esp_log_timestamp();
    snake_pause_start = 0;
    snake_bg_drawn    = false;
}

// Draw (or erase) the snake border on top of the current album art frame.
// Call once per display loop tick after drawing the background image.
static void snake_tick(MatrixPanel_I2S_DMA *display, const uint8_t bg[64][64][3])
{
    uint32_t now = esp_log_timestamp();

    switch (snake_phase)
    {
    case SNAKE_FILL:
    {
        // Advance head by however many steps have elapsed
        uint32_t steps = (now - snake_last_step) / SNAKE_STEP_MS;
        if (steps > 0)
        {
            snake_last_step += steps * SNAKE_STEP_MS;
            int new_head = snake_head + (int)steps;
            if (new_head > SNAKE_PERIMETER)
                new_head = SNAKE_PERIMETER;

            // Light up new pixels
            for (int i = snake_head; i < new_head; i++)
            {
                int x, y;
                snake_perimeter_to_xy(i, &x, &y);
                display->drawPixel(x, y, display->color565(255, 255, 255));
            }
            snake_head = new_head;
        }

        if (snake_head >= SNAKE_PERIMETER)
        {
            snake_phase       = SNAKE_PAUSE_AFTER_FILL;
            snake_pause_start = now;
        }
        break;
    }

    case SNAKE_PAUSE_AFTER_FILL:
        if ((now - snake_pause_start) >= SNAKE_PAUSE_MS)
        {
            snake_phase     = SNAKE_ERASE;
            snake_tail      = 0;
            snake_last_step = now;
        }
        break;

    case SNAKE_ERASE:
    {
        uint32_t steps = (now - snake_last_step) / SNAKE_STEP_MS;
        if (steps > 0)
        {
            snake_last_step += steps * SNAKE_STEP_MS;
            int new_tail = snake_tail + (int)steps;
            if (new_tail > SNAKE_PERIMETER)
                new_tail = SNAKE_PERIMETER;

            // Restore pixels from background image
            for (int i = snake_tail; i < new_tail; i++)
            {
                int x, y;
                snake_perimeter_to_xy(i, &x, &y);
                display->drawPixel(x, y,
                    display->color565(bg[y][x][0], bg[y][x][1], bg[y][x][2]));
            }
            snake_tail = new_tail;
        }

        if (snake_tail >= SNAKE_PERIMETER)
        {
            snake_phase       = SNAKE_PAUSE_AFTER_ERASE;
            snake_pause_start = now;
        }
        break;
    }

    case SNAKE_PAUSE_AFTER_ERASE:
        if ((now - snake_pause_start) >= SNAKE_PAUSE_MS)
            snake_reset(); // restart the loop
        break;
    }
}

// ---------------------------------------------------------------------------

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

void matrix_display_set_state(MatrixUiState state)
{
    ui_state = state;
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

static void draw_image(MatrixPanel_I2S_DMA *display, const uint8_t img[64][64][3])
{
    for (int y = 0; y < DISPLAY_HEIGHT; y++)
        for (int x = 0; x < DISPLAY_WIDTH; x++)
            display->drawPixel(x, y,
                               display->color565(img[y][x][0], img[y][x][1], img[y][x][2]));
}

static void draw_image_dimmed(MatrixPanel_I2S_DMA *display, const uint8_t img[64][64][3], float brightness_scale)
{
    for (int y = 0; y < DISPLAY_HEIGHT; y++)
        for (int x = 0; x < DISPLAY_WIDTH; x++)
            display->drawPixel(x, y,
                               display->color565(
                                   (uint8_t)(img[y][x][0] * brightness_scale),
                                   (uint8_t)(img[y][x][1] * brightness_scale),
                                   (uint8_t)(img[y][x][2] * brightness_scale)));
}

static void display_task(void *pvParameters)
{
    MatrixPanel_I2S_DMA *display = (MatrixPanel_I2S_DMA *)pvParameters;

    static uint8_t current_art[64][64][3];
    int applied_brightness = brightness;

    // Black canvas used as the snake background before any album art arrives
    static uint8_t black_art[64][64][3] = {};

    snake_reset(); // also clears snake_bg_drawn

    while (true)
    {
        if (applied_brightness != brightness)
        {
            applied_brightness = brightness;
            display->setBrightness8(applied_brightness);
        }

        switch (ui_state)
        {
        case MATRIX_UI_BOOT:
        {
            uint32_t now = esp_log_timestamp();
            float scale = 1.0f;

            if (boot_anim_state == BOOT_FADE_IN)
            {
                if (boot_anim_start == 0)
                    boot_anim_start = now;

                float t = (float)(now - boot_anim_start) / boot_fade_in_ms;
                if (t > 1.0f)
                    t = 1.0f;
                scale = ease_in_out(t);

                if (t >= 1.0f)
                {
                    boot_anim_state = BOOT_HOLD;
                    boot_anim_start = now;
                }
            }
            else if (boot_anim_state == BOOT_HOLD)
            {
                scale = 1.0f;

                if ((now - boot_anim_start) >= boot_hold_ms)
                {
                    boot_anim_state = BOOT_FADE_OUT;
                    boot_anim_start = now;
                }
            }
            else if (boot_anim_state == BOOT_FADE_OUT)
            {
                float t = (float)(now - boot_anim_start) / boot_fade_out_ms;
                if (t > 1.0f)
                    t = 1.0f;
                scale = ease_in_out(1.0f - t);

                if (t >= 1.0f)
                    boot_anim_state = BOOT_DONE;
            }
            else // BOOT_DONE
            {
                display->clearScreen();
                break;
            }

            draw_image_dimmed(display, TuneFrame, scale);
            break;
        }

        case MATRIX_UI_WIFI_WAIT:
        {
            // WIFI text
            break;
        }

        case MATRIX_UI_FAULT:
            draw_image(display, WifiFault);
            break;

        case MATRIX_UI_PLAYBACK:
        {
            if (anim_state == IDLE)
            {
                xSemaphoreTake(sonos_album_art_mutex, portMAX_DELAY);
                memcpy(current_art, sonos_get_album_art_64(), sizeof(current_art));
                xSemaphoreGive(sonos_album_art_mutex);

                if (!has_previous)
                {
                    // Check if art has arrived yet (non-black frame)
                    bool art_arrived = album_art_changed(black_art, current_art);

                    if (art_arrived)
                    {
                        // First art is ready – store it and fall through to
                        // normal playback on the next tick.
                        memcpy(prev_album_art, current_art, sizeof(prev_album_art));
                        has_previous = true;
                        snake_reset();
                    }
                    else
                    {
                        // Still waiting – show black screen with snake border.
                        // Only redraw the background at the start of each cycle
                        // so snake pixels are not wiped every frame.
                        if (!snake_bg_drawn)
                        {
                            draw_image(display, black_art);
                            snake_bg_drawn = true;
                        }
                        snake_tick(display, black_art);
                    }
                }
                else if (album_art_changed(prev_album_art, current_art))
                {
                    // New track – kick off the slide transition.
                    snake_reset(); // clears snake_bg_drawn for next wait period
                    memcpy(next_album_art, current_art, sizeof(next_album_art));
                    anim_state = SLIDE_OUT_OLD;
                    anim_start_time = esp_log_timestamp();
                }
                else
                {
                    // Album art loaded and stable – just display it, no snake.
                    draw_image(display, prev_album_art);
                }
            }
            else if (anim_state == SLIDE_OUT_OLD)
            {
                float t = get_t(anim_start_time, anim_duration_ms);
                int offset = (int)(t * -DISPLAY_HEIGHT);

                draw_composite(display, prev_album_art, offset, next_album_art, DISPLAY_HEIGHT);

                if (t >= 1.0f)
                {
                    anim_state = PAUSE_ANIM;
                    anim_start_time = esp_log_timestamp();
                }
            }
            else if (anim_state == PAUSE_ANIM)
            {
                draw_composite(display, prev_album_art, DISPLAY_HEIGHT, next_album_art, DISPLAY_HEIGHT);

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
                    has_previous = true;
                    anim_state = IDLE;
                }
            }
            break;
        }
        } // switch

        vTaskDelay(pdMS_TO_TICKS(16));
    }
}

#ifdef __cplusplus
extern "C"
{
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