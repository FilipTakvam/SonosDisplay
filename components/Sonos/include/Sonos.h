#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XML_MALLOC_SIZE 16384

typedef struct {
    char Ipv4[16];
    char deviceName[16];
} sonos_device_t;

// void sonos_discovery_start(sonos_device_t **devices_out, int *count_out);
sonos_device_t *sonos_find_device(const char *name);

#ifdef __cplusplus
}
#endif