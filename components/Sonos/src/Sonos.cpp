#include "Sonos.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_crt_bundle.h"
#include "tjpgd.h"

#include "tinyxml2.h"

static const char *TAG = "SONOS";

#define SSDP_IP "239.255.255.250"
#define SSDP_PORT 1900

static const char *M_SEARCH_MSG =
    "M-SEARCH * HTTP/1.1\r\n"
    "HOST: 239.255.255.250:1900\r\n"
    "MAN: \"ssdp:discover\"\r\n"
    "MX: 2\r\n"
    "ST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n"
    "\r\n";

static bool decode_album_art(const char *url);

SemaphoreHandle_t sonos_album_art_mutex = NULL;

static bool parse_room_name(const char *xml, char *room_name_out, size_t room_name_size)
{
    tinyxml2::XMLDocument doc;

    if (doc.Parse(xml) != tinyxml2::XML_SUCCESS)
    {
        ESP_LOGE(TAG, "Failed to parse XML");
        return false;
    }

    tinyxml2::XMLElement *root = doc.RootElement();
    tinyxml2::XMLElement *device = root ? root->FirstChildElement("device") : nullptr;
    tinyxml2::XMLElement *roomName = device ? device->FirstChildElement("roomName") : nullptr;

    if (!roomName || !roomName->GetText())
    {
        ESP_LOGE(TAG, "roomName not found in XML");
        return false;
    }

    strncpy(room_name_out, roomName->GetText(), room_name_size - 1);
    room_name_out[room_name_size - 1] = '\0';

    ESP_LOGI(TAG, "Room name: %s", room_name_out);
    return true;
}

static bool fetch_device_description(const char *url, char *xml_out, size_t xml_size)
{
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 3000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (!client)
    {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    esp_http_client_fetch_headers(client);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int read = esp_http_client_read(client, xml_out, xml_size - 1);

    if (read <= 0)
    {
        ESP_LOGE(TAG, "Failed to read HTTP response");
        esp_http_client_cleanup(client);
        return false;
    }

    xml_out[read] = '\0';

    esp_http_client_cleanup(client);
    return true;
}

static void send_msearch(int sock)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(SSDP_PORT);
    inet_pton(AF_INET, SSDP_IP, &addr.sin_addr);

    sendto(sock,
           M_SEARCH_MSG,
           strlen(M_SEARCH_MSG),
           0,
           (struct sockaddr *)&addr,
           sizeof(addr));

    ESP_LOGI(TAG, "M-SEARCH sent");
}

static sonos_device_t *listen_responses(int sock, int *count_out)
{
    char buffer[2048];
    struct sockaddr_in source_addr;
    socklen_t addr_len = sizeof(source_addr);
    sonos_device_t *devices = NULL;
    int deviceCount = 0;

    while (1)
    {
        int len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                           (struct sockaddr *)&source_addr, &addr_len);

        if (len > 0)
        {
            buffer[len] = 0;

            char IPv4[16];
            inet_ntop(AF_INET, &source_addr.sin_addr, IPv4, sizeof(IPv4));

            char deviceDescriptionURL[64];
            snprintf(deviceDescriptionURL, sizeof(deviceDescriptionURL), "http://%s:1400/xml/device_description.xml", IPv4);

            char *xml = (char *)malloc(XML_MALLOC_SIZE);
            if (!xml)
            {
                ESP_LOGE(TAG, "Out of memory of the XML buufer");
                continue;
            }

            if (!fetch_device_description(deviceDescriptionURL, xml, XML_MALLOC_SIZE))
            {
                free(xml);
                continue;
            }

            char deviceName[64];
            if (!parse_room_name(xml, deviceName, sizeof(deviceName)))
            {
                free(xml);
                continue;
            }

            free(xml);

            bool duplicate = false;

            for (int i = 0; i < deviceCount; i++)
            {
                if (strcmp(devices[i].Ipv4, IPv4) == 0)
                {
                    duplicate = true;
                    break;
                }
            }

            if (!duplicate)
            {
                sonos_device_t *tmp = (sonos_device_t *)realloc(devices, (deviceCount + 1) * sizeof(sonos_device_t));

                if (!tmp)
                {
                    ESP_LOGE(TAG, "Out of memory while discovering Sonos devices");
                    break;
                }

                devices = tmp;
                strncpy(devices[deviceCount].Ipv4, IPv4, sizeof(devices[deviceCount].Ipv4) - 1);
                devices[deviceCount].Ipv4[sizeof(devices[deviceCount].Ipv4) - 1] = 0;

                strncpy(devices[deviceCount].deviceName, deviceName, sizeof(devices[deviceCount].deviceName) - 1);
                devices[deviceCount].deviceName[sizeof(devices[deviceCount].deviceName) - 1] = '\0';
                ESP_LOGI(TAG, "Found Sonos device with IPv4 %s", devices[deviceCount].Ipv4);
                deviceCount++;
            }
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                ESP_LOGI(TAG, "Discovery complete - found %d device(s)", deviceCount);
            }
            else
            {
                ESP_LOGE(TAG, "recv error: %d", errno);
            }
            break;
        }
    }

    *count_out = deviceCount;
    return devices;
}

sonos_device_t *sonos_find_device(const char *name)
{
    sonos_device_t *devices = NULL;
    int count = 0;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

    if (sock < 0)
    {
        ESP_LOGE(TAG, "Failed to create socket");
        return NULL;
    }

    int ttl = 2;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    struct timeval timeout = {.tv_sec = 3, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    send_msearch(sock);
    devices = listen_responses(sock, &count);

    close(sock);

    for (int i = 0; i < count; i++)
    {
        if (strcmp(devices[i].deviceName, name) == 0)
        {
            sonos_device_t *found = (sonos_device_t *)malloc(sizeof(sonos_device_t));
            if (found)
                memcpy(found, &devices[i], sizeof(sonos_device_t));
            free(devices);
            return found;
        }
    }

    ESP_LOGW(TAG, "No device named '%s' found", name);
    free(devices);
    return NULL;
}

static bool retrieve_own_ip(char *ip_out, size_t ip_size)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

    if (!netif)
    {
        ESP_LOGE(TAG, "Failed to get netif handle");
        return false;
    }

    esp_netif_ip_info_t ip_info;

    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get the IP info of the client");
        return false;
    }

    esp_ip4addr_ntoa(&ip_info.ip, ip_out, ip_size);
    return true;
}

static httpd_handle_t notifyServer = NULL;

static char last_track_uri[256] = {0};
static char last_album_art_uri[256] = {0};

static bool parse_notify(const char *body, char *album_art_uri_out, size_t album_art_uri_size,
                         char *track_uri_out, size_t track_uri_size)
{
    tinyxml2::XMLDocument outer;
    if (outer.Parse(body) != tinyxml2::XML_SUCCESS)
    {
        ESP_LOGE(TAG, "Failed to parse outer NOTIFY XML");
        return false;
    }

    tinyxml2::XMLElement *propertyset = outer.RootElement();
    tinyxml2::XMLElement *property = propertyset ? propertyset->FirstChildElement("e:property") : nullptr;
    tinyxml2::XMLElement *lastChange = property ? property->FirstChildElement("LastChange") : nullptr;

    if (!lastChange || !lastChange->GetText())
    {
        ESP_LOGE(TAG, "LastChange not found");
        return false;
    }

    tinyxml2::XMLDocument inner;
    if (inner.Parse(lastChange->GetText()) != tinyxml2::XML_SUCCESS)
    {
        ESP_LOGE(TAG, "Failed to parse LastChange XML");
        return false;
    }

    tinyxml2::XMLElement *event = inner.RootElement();
    tinyxml2::XMLElement *instanceID = event ? event->FirstChildElement("InstanceID") : nullptr;

    if (!instanceID)
    {
        ESP_LOGE(TAG, "InstanceID not found");
        return false;
    }

    tinyxml2::XMLElement *trackURI = instanceID->FirstChildElement("CurrentTrackURI");
    if (!trackURI || !trackURI->Attribute("val"))
    {
        ESP_LOGE(TAG, "CurrentTrackURI not found");
        return false;
    }
    strncpy(track_uri_out, trackURI->Attribute("val"), track_uri_size - 1);
    track_uri_out[track_uri_size - 1] = '\0';

    tinyxml2::XMLElement *metaData = instanceID->FirstChildElement("CurrentTrackMetaData");
    if (!metaData || !metaData->Attribute("val"))
    {
        ESP_LOGE(TAG, "CurrentTrackMetaData not found");
        return false;
    }

    tinyxml2::XMLDocument didl;
    if (didl.Parse(metaData->Attribute("val")) != tinyxml2::XML_SUCCESS)
    {
        ESP_LOGE(TAG, "Failed to parse DIDL-Lite XML");
        return false;
    }

    tinyxml2::XMLElement *item = didl.RootElement() ? didl.RootElement()->FirstChildElement("item") : nullptr;
    tinyxml2::XMLElement *albumArt = item ? item->FirstChildElement("upnp:albumArtURI") : nullptr;

    if (!albumArt || !albumArt->GetText())
    {
        ESP_LOGE(TAG, "albumArtURI not found");
        return false;
    }

    strncpy(album_art_uri_out, albumArt->GetText(), album_art_uri_size - 1);
    album_art_uri_out[album_art_uri_size - 1] = '\0';

    return true;
}

static esp_err_t notify_handler(httpd_req_t *req)
{
    char *body = (char *)malloc(NOTIFY_MALLOC_SIZE);

    if (!body)
    {
        ESP_LOGE(TAG, "Out of memory for the NOTIFY body");
        return ESP_FAIL;
    }

    int received = 0;
    int chunk;
    while ((chunk = httpd_req_recv(req, body + received, NOTIFY_MALLOC_SIZE - 1 - received)) > 0)
    {
        received += chunk;
    }

    body[received] = 0;

    char track_uri[256] = {0};
    char album_art_uri[256] = {0};

    if (parse_notify(body, album_art_uri, sizeof(album_art_uri),
                     track_uri, sizeof(track_uri)))
    {
        if (strcmp(track_uri, last_track_uri) != 0)
        {
            strncpy(last_track_uri, track_uri, sizeof(last_track_uri) - 1);
            ESP_LOGI(TAG, "Album art uri: %s", album_art_uri);
            if (strcmp(last_album_art_uri, album_art_uri) != 0) {
                decode_album_art(album_art_uri);
                strncpy(last_album_art_uri, album_art_uri, sizeof(last_album_art_uri) - 1);
            } 
            else{
                ESP_LOGI(TAG, "Same album art uri as previous track - skipping to update");
            }
        }
    }

    free(body);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

void sonos_start_notify(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8080;
    config.stack_size = 16384;

    if (httpd_start(&notifyServer, &config) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start HTTP server for NOTIFY");
        return;
    }

    httpd_uri_t notifyURI = {
        .uri = "/notify",
        .method = (httpd_method_t)25,
        .handler = notify_handler,
    };

    httpd_register_uri_handler(notifyServer, &notifyURI);
    ESP_LOGI(TAG, "Notify server started on port 8080");
}

void sonos_subscribe(const sonos_device_t *device)
{
    char url[64];
    snprintf(url, sizeof(url), "http://%s:1400/MediaRenderer/AVTransport/Event", device->Ipv4);

    char ownIP[16];

    if (!retrieve_own_ip(ownIP, sizeof(ownIP)))
    {
        return;
    }

    char callback[64];
    snprintf(callback, sizeof(callback), "<http://%s:8080/notify>", ownIP);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_SUBSCRIBE,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (!client)
    {
        ESP_LOGE(TAG, "Failed to initialize http client for SUBSCRIBE");
        return;
    }

    esp_http_client_set_header(client, "callback", callback);
    esp_http_client_set_header(client, "NT", "upnp:event");
    esp_http_client_set_header(client, "Timeout", "Second-3600");

    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "SUBSCRIBE event failed");
    }

    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "SUBSCRIBE event response %d", status);

    esp_http_client_cleanup(client);
}

typedef struct {
    esp_http_client_handle_t client;
    uint8_t chunk[512];
} jpeg_http_input_t;

static uint8_t rgb_matrix[80][80][3];

static int jpeg_output_callback(JDEC *jd, void *bitmap, JRECT *rect)
{
    uint8_t *pixels = (uint8_t*)bitmap;
    int pixel_idx = 0;

    for (int y = rect->top; y <= rect->bottom; y++)
    {
        for (int x = rect->left; x <= rect->right; x++)
        {
            if (x < 80 && y < 80)
            {
                rgb_matrix[y][x][0] = pixels[pixel_idx + 0];
                rgb_matrix[y][x][1] = pixels[pixel_idx + 1];
                rgb_matrix[y][x][2] = pixels[pixel_idx + 2];
            }
            pixel_idx += 3;
        }
    }
    return 1;
}

static uint8_t rgb_matrix_64[64][64][3];

static void downscale_80_to_64(void)
{
    for (int y = 0; y < 64; y++)
    {
        for (int x = 0; x < 64; x++)
        {
            float src_x = (x / 64.0f) * 80.0f;
            float src_y = (y / 64.0f) * 80.0f;

            int x0 = (int)src_x;
            int y0 = (int)src_y;
            int x1 = x0 + 1 < 80 ? x0 + 1 : x0;
            int y1 = y0 + 1 < 80 ? y0 + 1 : y0;

            float fx = src_x - x0;
            float fy = src_y - y0;

            for (int c = 0; c < 3; c++)
            {
                float top    = rgb_matrix[y0][x0][c] * (1 - fx) + rgb_matrix[y0][x1][c] * fx;
                float bottom = rgb_matrix[y1][x0][c] * (1 - fx) + rgb_matrix[y1][x1][c] * fx;
                rgb_matrix_64[y][x][c] = (uint8_t)(top * (1 - fy) + bottom * fy);
            }
        }
    }
}

uint8_t (*sonos_get_album_art_64(void))[64][3]
{
    return rgb_matrix_64;
}

static size_t jpeg_input_callback(JDEC *jd, uint8_t *buf, size_t len)
{
    jpeg_http_input_t *input = (jpeg_http_input_t *)jd->device;

    size_t total_read = 0;

    while (total_read < len)
    {
        int read = esp_http_client_read(
            input->client,
            (char *)input->chunk + total_read,
            len - total_read);

        if (read > 0)
        {
            total_read += read;
        }
        else if (read == 0)
        {
            // Connection closed cleanly
            break;
        }
        else
        {
            // read < 0
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // Transient — give the TCP stack a moment and retry
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            // A real error
            ESP_LOGE(TAG, "jpeg_input_callback: read error, errno=%d", errno);
            break;
        }
    }

    if (buf && total_read > 0)
        memcpy(buf, input->chunk, total_read);

    return total_read;
}

static uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r >> 3) << 11) |  // 5 bits red
           ((g >> 2) << 5)  |  // 6 bits green
           (b >> 3);           // 5 bits blue
}

void sonos_log_album_art_64(void)
{
xSemaphoreTake(sonos_album_art_mutex, portMAX_DELAY);

    printf("{\n");

    for (int y = 0; y < 64; y++)
    {
        for (int x = 0; x < 64; x++)
        {
            uint16_t rgb565 = rgb888_to_rgb565(
                rgb_matrix_64[y][x][0],
                rgb_matrix_64[y][x][1],
                rgb_matrix_64[y][x][2]);

            printf("0x%04X", rgb565);

            if (!(y == 63 && x == 63))
                printf(",");

            if (((y * 64 + x + 1) % 16) == 0)
                printf("\n");
            else
                printf(" ");
        }
    }

    printf("};\n");

    xSemaphoreGive(sonos_album_art_mutex);
}

static bool decode_album_art(const char *url)
{
    esp_http_client_config_t config = {
        .url               = url,
        .timeout_ms        = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (!client)
    {
        ESP_LOGE(TAG, "Failed to init HTTP client for album art");
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open album art connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    esp_http_client_fetch_headers(client);

    JDEC jd;
    jpeg_http_input_t input = { .client = client };

    void *work = malloc(TJPGD_WORK_SIZE);
    if (!work)
    {
        ESP_LOGE(TAG, "Out of memory for TJpgDec workspace");
        esp_http_client_cleanup(client);
        return false;
    }

    JRESULT res = jd_prepare(&jd, jpeg_input_callback, work, TJPGD_WORK_SIZE, &input);
    if (res != JDR_OK)
    {
        ESP_LOGE(TAG, "jd_prepare failed: %d", res);
        free(work);
        esp_http_client_cleanup(client);
        return false;
    }

    res = jd_decomp(&jd, jpeg_output_callback, 3);
    if (res != JDR_OK)
    {
        ESP_LOGE(TAG, "jd_decomp failed: %d", res);
        free(work);
        esp_http_client_cleanup(client);
        return false;
    }

    xSemaphoreTake(sonos_album_art_mutex, portMAX_DELAY);
    downscale_80_to_64();
    xSemaphoreGive(sonos_album_art_mutex);

    // sonos_log_album_art_64();

    free(work);
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "Album art decoded successfully");
    return true;
}