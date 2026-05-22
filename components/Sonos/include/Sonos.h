#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char Ipv4[16];
} sonos_device_t;

void sonos_discovery_start(sonos_device_t **devices_out, int *count_out);

#ifdef __cplusplus
}
#endif