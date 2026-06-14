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
"<!DOCTYPE html>\n"
"<html lang=\"en\">\n"
"\n"
"<head>\n"
"    <meta charset=\"UTF-8\">\n"
"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
"    <title>TUNEFRAME Setup</title>\n"
"    <style>\n"
"        :root {\n"
"            --bg: #0a0a0a;\n"
"            --card: #161616;\n"
"            --border: #2e2e2e;\n"
"            --text: #f5f5f5;\n"
"            --muted: #8a8a8a;\n"
"            --accent: #f5f5f5;\n"
"            --radius: 10px;\n"
"        }\n"
"\n"
"        * {\n"
"            box-sizing: border-box;\n"
"        }\n"
"\n"
"        body {\n"
"            margin: 0;\n"
"            font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\", Roboto, Helvetica, Arial, sans-serif;\n"
"            background: var(--bg);\n"
"            color: var(--text);\n"
"            display: flex;\n"
"            flex-direction: column;\n"
"            align-items: center;\n"
"            justify-content: center;\n"
"            min-height: 100vh;\n"
"            padding: 24px;\n"
"        }\n"
"\n"
"        .wordmark {\n"
"            font-size: 22px;\n"
"            font-weight: 700;\n"
"            letter-spacing: 0.18em;\n"
"            margin-bottom: 24px;\n"
"            color: var(--text);\n"
"        }\n"
"\n"
"        .card {\n"
"            width: 100%;\n"
"            max-width: 380px;\n"
"            background: var(--card);\n"
"            border: 1px solid var(--border);\n"
"            border-radius: var(--radius);\n"
"            padding: 32px 28px;\n"
"        }\n"
"\n"
"        .header {\n"
"            display: flex;\n"
"            align-items: center;\n"
"            gap: 10px;\n"
"            margin-bottom: 4px;\n"
"        }\n"
"\n"
"        .header svg {\n"
"            flex-shrink: 0;\n"
"        }\n"
"\n"
"        h1 {\n"
"            font-size: 18px;\n"
"            font-weight: 600;\n"
"            margin: 0;\n"
"            letter-spacing: 0.06em;\n"
"        }\n"
"\n"
"        p.subtitle {\n"
"            color: var(--muted);\n"
"            font-size: 13px;\n"
"            margin: 6px 0 28px;\n"
"            line-height: 1.5;\n"
"        }\n"
"\n"
"        label {\n"
"            display: block;\n"
"            font-size: 13px;\n"
"            font-weight: 500;\n"
"            margin-bottom: 6px;\n"
"            color: var(--text);\n"
"        }\n"
"\n"
"        .field {\n"
"            margin-bottom: 18px;\n"
"        }\n"
"\n"
"        input {\n"
"            width: 100%;\n"
"            padding: 11px 12px;\n"
"            border-radius: 8px;\n"
"            border: 1px solid var(--border);\n"
"            background: #0f0f0f;\n"
"            color: var(--text);\n"
"            font-size: 15px;\n"
"            outline: none;\n"
"            transition: border-color 0.15s ease;\n"
"        }\n"
"\n"
"        input:focus {\n"
"            border-color: var(--accent);\n"
"        }\n"
"\n"
"        input::placeholder {\n"
"            color: #555;\n"
"        }\n"
"\n"
"        button {\n"
"            width: 100%;\n"
"            padding: 13px;\n"
"            margin-top: 8px;\n"
"            border-radius: 8px;\n"
"            border: none;\n"
"            background: var(--accent);\n"
"            color: #0a0a0a;\n"
"            font-size: 15px;\n"
"            font-weight: 600;\n"
"            cursor: pointer;\n"
"            transition: background 0.15s ease;\n"
"        }\n"
"\n"
"        button:hover {\n"
"            background: #d8d8d8;\n"
"        }\n"
"\n"
"        button:focus-visible,\n"
"        input:focus-visible {\n"
"            outline: 2px solid var(--accent);\n"
"            outline-offset: 2px;\n"
"        }\n"
"    </style>\n"
"</head>\n"
"\n"
"<body>\n"
"    <div class=\"wordmark\">TUNEFRAME</div>\n"
"    <div class=\"card\">\n"
"        <div class=\"header\">\n"
"            <svg width=\"22\" height=\"22\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#f5f5f5\"\n"
"                stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">\n"
"                <path d=\"M5 12.55a11 11 0 0 1 14.08 0\" />\n"
"                <path d=\"M1.42 9a16 16 0 0 1 21.16 0\" />\n"
"                <path d=\"M8.53 16.11a6 6 0 0 1 6.95 0\" />\n"
"                <line x1=\"12\" y1=\"20\" x2=\"12\" y2=\"20\" />\n"
"            </svg>\n"
"            <h1>Network Setup</h1>\n"
"        </div>\n"
"        <p class=\"subtitle\">Enter Wifi credentials and name of speaker</p>\n"
"        <form method=\"POST\" action=\"/save\">\n"
"            <div class=\"field\">\n"
"                <label for=\"ssid\">Network name (SSID)</label>\n"
"                <input id=\"ssid\" name=\"ssid\" type=\"text\" required autocomplete=\"off\" placeholder=\"e.g. Home WiFi\">\n"
"            </div>\n"
"            <div class=\"field\">\n"
"                <label for=\"pass\">Password</label>\n"
"                <input id=\"pass\" name=\"pass\" type=\"password\" autocomplete=\"off\" placeholder=\"Leave blank if none\">\n"
"            </div>\n"
"            <div class=\"field\">\n"
"                <label for=\"sonos_name\">Speaker name</label>\n"
"                <input id=\"sonos_name\" name=\"sonos_name\" type=\"text\" required autocomplete=\"off\" placeholder=\"e.g. Living Room\">\n"
"            </div>\n"
"            <button type=\"submit\">Save</button>\n"
"        </form>\n"
"    </div>\n"
"</body>\n"
"\n"
"</html>";

static const char *HTML_SAVED =
"<!DOCTYPE html>\n"
"<html lang=\"en\">\n"
"\n"
"<head>\n"
"    <meta charset=\"UTF-8\">\n"
"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
"    <title>TUNEFRAME Setup</title>\n"
"    <style>\n"
"        :root {\n"
"            --bg: #0a0a0a;\n"
"            --card: #161616;\n"
"            --border: #2e2e2e;\n"
"            --text: #f5f5f5;\n"
"            --muted: #8a8a8a;\n"
"            --accent: #f5f5f5;\n"
"            --radius: 10px;\n"
"        }\n"
"\n"
"        * {\n"
"            box-sizing: border-box;\n"
"        }\n"
"\n"
"        body {\n"
"            margin: 0;\n"
"            font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\", Roboto, Helvetica, Arial, sans-serif;\n"
"            background: var(--bg);\n"
"            color: var(--text);\n"
"            display: flex;\n"
"            flex-direction: column;\n"
"            align-items: center;\n"
"            justify-content: center;\n"
"            min-height: 100vh;\n"
"            padding: 24px;\n"
"            text-align: center;\n"
"        }\n"
"\n"
"        .wordmark {\n"
"            font-size: 22px;\n"
"            font-weight: 700;\n"
"            letter-spacing: 0.18em;\n"
"            margin-bottom: 24px;\n"
"            color: var(--text);\n"
"        }\n"
"\n"
"        .card {\n"
"            width: 100%;\n"
"            max-width: 380px;\n"
"            background: var(--card);\n"
"            border: 1px solid var(--border);\n"
"            border-radius: var(--radius);\n"
"            padding: 32px 28px;\n"
"        }\n"
"\n"
"        .icon {\n"
"            margin-bottom: 16px;\n"
"        }\n"
"\n"
"        h1 {\n"
"            font-size: 18px;\n"
"            font-weight: 600;\n"
"            margin: 0 0 8px;\n"
"            letter-spacing: 0.06em;\n"
"        }\n"
"\n"
"        p {\n"
"            color: var(--muted);\n"
"            font-size: 13px;\n"
"            line-height: 1.6;\n"
"            margin: 0;\n"
"        }\n"
"    </style>\n"
"</head>\n"
"\n"
"<body>\n"
"    <div class=\"wordmark\">TUNEFRAME</div>\n"
"    <div class=\"card\">\n"
"        <div class=\"icon\">\n"
"            <svg width=\"40\" height=\"40\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#f5f5f5\" stroke-width=\"2\"\n"
"                stroke-linecap=\"round\" stroke-linejoin=\"round\">\n"
"                <circle cx=\"12\" cy=\"12\" r=\"10\" />\n"
"                <path d=\"M9 12l2 2 4-4\" />\n"
"            </svg>\n"
"        </div>\n"
"        <h1>Network saved</h1>\n"
"        <p>Power the display off and on. Album art streaming will be available after it reconnects if everything has been configured correctly.<br><br>You can\n"
"            close this page.</p>\n"
"    </div>\n"
"</body>\n"
"\n"
"</html>";


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