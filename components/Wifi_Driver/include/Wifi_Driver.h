#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_CONNECTED_BIT BIT0

extern EventGroupHandle_t wifi_event_group;

void wifi_driver_init(void);

#ifdef __cplusplus
}
#endif