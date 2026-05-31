#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MATRIX_UI_BOOT,
    MATRIX_UI_WIFI_WAIT,
    MATRIX_UI_FAULT,
    MATRIX_UI_PLAYBACK,
} MatrixUiState;

typedef enum
{
    IDLE,
    SLIDE_OUT_OLD,
    PAUSE_ANIM,
    SLIDE_IN_NEW
} AnimationState;

typedef enum {
    BOOT_FADE_IN,
    BOOT_HOLD,
    BOOT_FADE_OUT,
    BOOT_DONE,
} BootAnimState;


void matrix_display_init();
void matrix_display_start_task();
void matrix_display_set_state(MatrixUiState state);
void matrix_display_set_brightness(int brightness);
int matrix_display_get_brightness();

#ifdef __cplusplus
}
#endif
