#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XML_MALLOC_SIZE 16384
#define NOTIFY_MALLOC_SIZE 8192

typedef struct {
    char Ipv4[16];
    char deviceName[16];
} sonos_device_t;

sonos_device_t *sonos_find_device(const char *name);

void sonos_start_notify(void);
void sonos_subscribe(const sonos_device_t *device);

#ifdef __cplusplus
}
#endif