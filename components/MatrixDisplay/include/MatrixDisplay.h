#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void matrix_display_init();
void matrix_display_start_task();
void matrix_display_set_brightness(int brightness);
int matrix_display_get_brightness();

#ifdef __cplusplus
}
#endif
