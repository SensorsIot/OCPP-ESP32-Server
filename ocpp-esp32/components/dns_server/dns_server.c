#include "dns_server.h"

#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/ip4_addr.h"

#include <string.h>

static const char *TAG = "dns_srv";

#define DNS_PORT       53
#define DNS_BUF_SIZE   512
#define DNS_TASK_STACK 3072

/* DNS header (RFC 1035) */
typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
} dns_header_t;

static int s_sock = -1;
static TaskHandle_t s_task;
static uint32_t s_redirect_addr;

/* Skip over a DNS name (sequence of labels) and return bytes consumed.
 * Does not follow compression pointers — for the question section only.
 */
static int skip_dns_name(const uint8_t *buf, int buf_len, int offset)
{
    int pos = offset;
    while (pos < buf_len) {
        uint8_t len = buf[pos];
        if (len == 0) {
            pos++;
            break;
        }
        if ((len & 0xC0) == 0xC0) {
            /* compression pointer — 2 bytes */
            pos += 2;
            break;
        }
        pos += 1 + len;
    }
    return pos - offset;
}

static void dns_task(void *arg)
{
    (void)arg;
    uint8_t buf[DNS_BUF_SIZE];

    ESP_LOGI(TAG, "DNS server running on port %d", DNS_PORT);

    while (1) {
        struct sockaddr_in client;
        socklen_t client_len = sizeof(client);

        int len = recvfrom(s_sock, buf, sizeof(buf), 0,
                           (struct sockaddr *)&client, &client_len);
        if (len < 0) {
            if (errno == EBADF || errno == EINVAL) {
                break; /* socket closed, exit */
            }
            ESP_LOGW(TAG, "recvfrom error: %d", errno);
            continue;
        }

        if (len < (int)sizeof(dns_header_t)) {
            continue;
        }

        dns_header_t *hdr = (dns_header_t *)buf;

        /* Only respond to standard queries (QR=0, Opcode=0) */
        uint16_t flags = ntohs(hdr->flags);
        if ((flags & 0x8000) != 0) continue; /* not a query */
        if (ntohs(hdr->qd_count) == 0) continue;

        /* Parse question: skip QNAME, read QTYPE */
        int qname_offset = sizeof(dns_header_t);
        int name_len = skip_dns_name(buf, len, qname_offset);
        int qtype_offset = qname_offset + name_len;

        if (qtype_offset + 4 > len) continue;

        (void)buf[qtype_offset]; /* qtype/qclass not needed — we answer all queries */

        /* Build response: copy query, set response flags, append answer */
        /* Set QR=1, AA=1, RA=1 */
        hdr->flags = htons(0x8580);
        hdr->an_count = htons(1);
        hdr->ns_count = 0;
        hdr->ar_count = 0;

        int resp_len = qtype_offset + 4; /* end of question section */

        /* Answer section: name pointer + type + class + TTL + rdlength + rdata */
        if (resp_len + 16 > (int)sizeof(buf)) continue;

        /* Name pointer to question name */
        buf[resp_len++] = 0xC0;
        buf[resp_len++] = (uint8_t)qname_offset;

        /* Type A (1) regardless of query type — captive portal trick */
        buf[resp_len++] = 0x00;
        buf[resp_len++] = 0x01;

        /* Class IN */
        buf[resp_len++] = 0x00;
        buf[resp_len++] = 0x01;

        /* TTL = 60 seconds */
        buf[resp_len++] = 0x00;
        buf[resp_len++] = 0x00;
        buf[resp_len++] = 0x00;
        buf[resp_len++] = 60;

        /* RDLENGTH = 4 */
        buf[resp_len++] = 0x00;
        buf[resp_len++] = 0x04;

        /* RDATA = IP address (network byte order) */
        memcpy(&buf[resp_len], &s_redirect_addr, 4);
        resp_len += 4;

        sendto(s_sock, buf, resp_len, 0,
               (struct sockaddr *)&client, client_len);
    }

    ESP_LOGI(TAG, "DNS server task exiting");
    vTaskDelete(NULL);
}

esp_err_t dns_server_start(const char *redirect_ip)
{
    ip4_addr_t addr;
    if (!ip4addr_aton(redirect_ip, &addr)) {
        ESP_LOGE(TAG, "Invalid IP: %s", redirect_ip);
        return ESP_ERR_INVALID_ARG;
    }
    s_redirect_addr = addr.addr;

    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "Socket create failed");
        return ESP_FAIL;
    }

    struct sockaddr_in server = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = INADDR_ANY,
    };

    if (bind(s_sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        ESP_LOGE(TAG, "Bind failed (port %d)", DNS_PORT);
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }

    BaseType_t ret = xTaskCreate(dns_task, "dns_srv", DNS_TASK_STACK,
                                  NULL, 5, &s_task);
    if (ret != pdPASS) {
        close(s_sock);
        s_sock = -1;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "DNS server started, redirect → %s", redirect_ip);
    return ESP_OK;
}

void dns_server_stop(void)
{
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }
    /* Task will exit on next recvfrom error */
    ESP_LOGI(TAG, "DNS server stopped");
}
