#include "Sonos.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

#include "esp_http_client.h"

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

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int content_length = esp_http_client_fetch_headers(client);

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

// The Simple Service Discovery Protocol (SSDP) M-SEARCH - multicast HTTP-over-UDP request
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

            char *xml = (char*)malloc(XML_MALLOC_SIZE);
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
            // Seems to be that a device can be reply more than once - skip duplicates
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

    // Gives an IPv4 UDP socket.(AF_INET = IPv4, SOCK_DGRAM = Datagram socket - UDP, IPPROTO_IP = default protocol for this socket type)
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

    if (sock < 0)
    {
        ESP_LOGE(TAG, "Failed to create socket");
        return NULL;
    }
    // Sets the TTL (Time To Live) for multicast packets.
    // TTL controls how many router hops a packet is allowed to make before being discarded. 2 means it can cross one router, which is enough for a local network.
    int ttl = 2;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    // Sets a receive timeout on the socket.
    // Without this, recvfrom in listen_responses would block forever waiting for a packet.
    // With it, recvfrom gives up and returns an error after 3 seconds, which is how we know discovery is finished.
    struct timeval timeout = {.tv_sec = 3, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    send_msearch(sock);
    devices = listen_responses(sock, &count);

    close(sock);

    // Find device matching name
    for (int i = 0; i < count; i++)
    {
        if (strcmp(devices[i].deviceName, name) == 0)
        {
            // Copy the match into a standalone heap allocation
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