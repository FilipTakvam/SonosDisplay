#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AP_SSID      "SonosDisplay"
#define AP_IP        "192.168.4.1"
#define MDNS_HOST    "sonosdisplay"
#define NVS_NAMESPACE "wifi_config"
#define NVS_KEY_SSID  "ssid"
#define NVS_KEY_PASS  "password"
#define NVS_KEY_SONOS_NAME "sonos_name"

void wifi_config_start_ap(void);
bool wifi_config_load(char *ssid_out, size_t ssid_size, char *pass_out, size_t pass_size);
bool wifi_config_load_sonos_name(char *name_out, size_t name_size);

#ifdef __cplusplus
}
#endif