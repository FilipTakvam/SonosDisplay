#include "Sonos.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

#include "tinyxml2.h"

static const char *TAG = "SONOS";

#define SSDP_IP   "239.255.255.250"
#define SSDP_PORT 1900

static const char *M_SEARCH_MSG =
"M-SEARCH * HTTP/1.1\r\n"
"HOST: 239.255.255.250:1900\r\n"
"MAN: \"ssdp:discover\"\r\n"
"MX: 2\r\n"
"ST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n"
"\r\n";


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
           (struct sockaddr*)&addr,
           sizeof(addr));

    ESP_LOGI(TAG, "M-SEARCH sent");
}

static void listen_responses(int sock)
{
    char buffer[1024];
    struct sockaddr_in source_addr;
    socklen_t addr_len = sizeof(source_addr);

    while (1)
    {
        int len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                           (struct sockaddr*)&source_addr, &addr_len);

        if (len > 0)
        {
            buffer[len] = 0;
            ESP_LOGI(TAG, "Response from %s:\n%s",
                     inet_ntoa(source_addr.sin_addr), buffer);
            break;
        }
        else
        {
            ESP_LOGE(TAG, "Timeout - No speaker responded");
            break;
        }
    }
}

void sonos_discovery_start()
{
    // Gives an IPv4 UDP socket.(AF_INET = IPv4, SOCK_DGRAM = Datagram socket - UDP, IPPROTO_IP = default protocol for this socket type)
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

    if (sock < 0)
    {
        ESP_LOGE(TAG, "Failed to create socket");
        return;
    }
    // Sets the TTL (Time To Live) for multicast packets.
    // TTL controls how many router hops a packet is allowed to make before being discarded. 2 means it can cross one router, which is enough for a local network.
    int ttl = 2;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    // Sets a receive timeout on the socket.
    // Without this, recvfrom in listen_responses would block forever waiting for a packet. 
    // With it, recvfrom gives up and returns an error after 3 seconds, which is how we know discovery is finished.
    struct timeval timeout = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    send_msearch(sock);
    listen_responses(sock);

    close(sock);
}