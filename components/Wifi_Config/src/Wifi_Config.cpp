#include "Wifi_Config.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "mdns.h"
#include "nvs.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "WIFI_CONFIG";

static const char *HTML =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>"
    "body{font-family:sans-serif;display:flex;flex-direction:column;align-items:center;padding:40px;background:#111;color:#eee}"
    "h2{margin-bottom:24px}"
    "label{width:100%;max-width:320px;display:block;margin-bottom:16px;font-size:14px}"
    "input{width:100%;padding:10px;margin-top:6px;box-sizing:border-box;border-radius:6px;border:none;font-size:16px}"
    "button{padding:12px 32px;margin-top:16px;border-radius:6px;border:none;background:#1db954;color:#fff;font-size:16px;cursor:pointer}"
    "</style></head>"
    "<body><h2>WiFi Setup</h2>"
    "<form method='POST' action='/save'>"
    "<label>Network name (SSID)<input name='ssid' type='text' required autocomplete='off'></label>"
    "<label>Password<input name='pass' type='password' autocomplete='off'></label>"
    "<label>Sonos Speaker Name<input name='sonos_name' type='text' required autocomplete='off'></label>"
    "<button type='submit'>Save &amp; Reboot</button>"
    "</form></body></html>";

static const char *HTML_SAVED =
    "<!DOCTYPE html><html><body style='font-family:sans-serif;text-align:center;padding:40px;background:#111;color:#eee'>"
    "<h2>Saved! Turn power off and on, and you will now be able to stream the album art</h2><p>Device is rebooting, you can close this page.</p>"
    "</body></html>";


static void url_decode(char *dst, const char *src, size_t dst_size)
{
    size_t i = 0;
    while (*src && i < dst_size - 1)
    {
        if (*src == '%' && src[1] && src[2])
        {
            char hex[3] = {src[1], src[2], 0};
            dst[i++] = (char)strtol(hex, NULL, 16);
            src += 3;
        }
        else if (*src == '+')
        {
            dst[i++] = ' ';
            src++;
        }
        else
        {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

static void parse_form_field(const char *body, const char *key, char *out, size_t out_size)
{
    char search[64];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(body, search);
    if (!p)
    {
        out[0] = '\0';
        return;
    }
    p += strlen(search);

    const char *end = strchr(p, '&');
    size_t len = end ? (size_t)(end - p) : strlen(p);

    if (len >= out_size)
    {
        len = out_size - 1;
    }

    char encoded[128] = {0};
    memcpy(encoded, p, len);
    url_decode(out, encoded, out_size);
}

static esp_err_t handle_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML, strlen(HTML));
    return ESP_OK;
}

static esp_err_t handle_save(httpd_req_t *req)
{
    char body[512] = {0};
    int received = httpd_req_recv(req, body, sizeof(body) -1);
    if  (received <= 0)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    body[received] = '\0';

    char ssid[64] = {0};
    char pass[64] = {0};
    char sonos_name[64] = {0};

    parse_form_field(body, "ssid", ssid, sizeof(ssid));
    parse_form_field(body, "pass", pass, sizeof(pass));
    parse_form_field(body, "sonos_name", sonos_name, sizeof(sonos_name));

    ESP_LOGI(TAG, "Saving SSID: %s", ssid);
    ESP_LOGI(TAG, "Saving Sonos name: %s", sonos_name);

    nvs_handle_t nvs;
    if(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK)
    {
        nvs_set_str(nvs, NVS_KEY_SSID, ssid);
        nvs_set_str(nvs, NVS_KEY_PASS, pass);
        nvs_set_str(nvs, NVS_KEY_SONOS_NAME, sonos_name);
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_SAVED, strlen(HTML_SAVED));

    vTaskDelay(pdMS_TO_TICKS(1500));
    return ESP_OK;
}

static esp_err_t handle_captive(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://" MDNS_HOST ".local/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static void init_mdns()
{
    mdns_init();
    mdns_hostname_set(MDNS_HOST);
    mdns_instance_name_set("Sonos Display WiFi Setup");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS started - http://%s.local", MDNS_HOST);
}

void wifi_config_start_ap()
{
    ESP_LOGI(TAG, "Starting AP...");

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t ap_config = {};
    strncpy((char*)ap_config.ap.ssid, AP_SSID, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = strlen(AP_SSID);
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    ap_config.ap.max_connection = 4;

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    esp_wifi_start();
    
    ESP_LOGI(TAG, "AP started with SSID: %s", AP_SSID);

    init_mdns();
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    httpd_start(&server, &config);

    httpd_uri_t root    = { .uri = "/",                      .method = HTTP_GET,  .handler = handle_root    , .user_ctx = NULL};
    httpd_uri_t save    = { .uri = "/save",                  .method = HTTP_POST, .handler = handle_save    , .user_ctx = NULL};
    httpd_uri_t cp_ios  = { .uri = "/hotspot-detect.html",   .method = HTTP_GET,  .handler = handle_captive , .user_ctx = NULL};
    httpd_uri_t cp_and  = { .uri = "/generate_204",          .method = HTTP_GET,  .handler = handle_captive , .user_ctx = NULL};
    httpd_uri_t cp_and2 = { .uri = "/connecttest.txt",       .method = HTTP_GET,  .handler = handle_captive , .user_ctx = NULL};
    httpd_uri_t cp_and3 = { .uri = "/redirect",              .method = HTTP_GET,  .handler = handle_captive , .user_ctx = NULL};

    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &save);
    httpd_register_uri_handler(server, &cp_ios);
    httpd_register_uri_handler(server, &cp_and);
    httpd_register_uri_handler(server, &cp_and2);
    httpd_register_uri_handler(server, &cp_and3);

    vTaskDelay(portMAX_DELAY);
}

bool wifi_config_load(char *ssid_out, size_t ssid_size, char *pass_out, size_t pass_size)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK)
        return false;

    bool ok = true;
    ok &= (nvs_get_str(nvs, NVS_KEY_SSID, ssid_out, &ssid_size) == ESP_OK);
    ok &= (nvs_get_str(nvs, NVS_KEY_PASS, pass_out, &pass_size) == ESP_OK);
    nvs_close(nvs);
    return ok;
}

bool wifi_config_load_sonos_name(char *name_out, size_t name_size)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK)
        return false;

    bool ok = (nvs_get_str(nvs, NVS_KEY_SONOS_NAME, name_out, &name_size) == ESP_OK);
    nvs_close(nvs);
    return ok;
}