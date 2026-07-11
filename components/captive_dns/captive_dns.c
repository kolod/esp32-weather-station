#include "captive_dns.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string.h>
#include <stdint.h>

#define TAG        "captive_dns"
#define DNS_PORT   53
#define BUF_SIZE   512

/* AP gateway IP to redirect all DNS queries to (4 bytes, big-endian) */
static const uint8_t REDIRECT_IP[4] = {192, 168, 4, 1};

static int           s_sock = -1;
static TaskHandle_t  s_task = NULL;
static volatile bool s_stop = false;

/* Minimal DNS response: copy question, set QR+AA flags, add one A record answer */
static int build_response(const uint8_t *req, int req_len,
                           uint8_t *resp, int resp_max)
{
    if (req_len < 12) return -1;

    /* Copy header; set QR=1, AA=1, RA=1, RCODE=0 */
    memcpy(resp, req, 12);
    resp[2] = 0x81; /* QR=1, Opcode=0, AA=1, TC=0, RD=1 */
    resp[3] = 0x80; /* RA=1, Z=0, RCODE=0              */
    resp[7] = 1;    /* ANCOUNT = 1                       */

    /* Copy question section verbatim */
    int qlen = 12;
    while (qlen < req_len && req[qlen] != 0) {
        qlen += req[qlen] + 1;
    }
    qlen += 5; /* trailing 0x00 + QTYPE (2) + QCLASS (2) */
    if (qlen > req_len) return -1;
    memcpy(resp + 12, req + 12, qlen - 12);

    /* Append answer RR: pointer to question name, A, IN, TTL 60, RDLENGTH 4, IP */
    int pos = qlen;
    if (pos + 16 > resp_max) return -1;
    resp[pos++] = 0xC0; resp[pos++] = 0x0C; /* pointer to name at offset 12 */
    resp[pos++] = 0x00; resp[pos++] = 0x01; /* TYPE A    */
    resp[pos++] = 0x00; resp[pos++] = 0x01; /* CLASS IN  */
    resp[pos++] = 0x00; resp[pos++] = 0x00; resp[pos++] = 0x00; resp[pos++] = 60; /* TTL */
    resp[pos++] = 0x00; resp[pos++] = 0x04; /* RDLENGTH  */
    memcpy(resp + pos, REDIRECT_IP, 4);     /* RDATA     */
    pos += 4;
    return pos;
}

static void dns_task(void *arg)
{
    uint8_t req[BUF_SIZE], resp[BUF_SIZE];
    struct sockaddr_in src;
    socklen_t src_len = sizeof(src);

    while (!s_stop) {
        int n = recvfrom(s_sock, req, sizeof(req), 0,
                         (struct sockaddr *)&src, &src_len);
        if (n < 0) {
            if (s_stop) break;
            continue;
        }
        int rlen = build_response(req, n, resp, sizeof(resp));
        if (rlen > 0) {
            sendto(s_sock, resp, rlen, 0, (struct sockaddr *)&src, src_len);
        }
    }
    close(s_sock);
    s_sock = -1;
    vTaskDelete(NULL);
}

esp_err_t captive_dns_start(void)
{
    s_stop = false;
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "socket() failed");
        return ESP_FAIL;
    }

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(s_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind() on port 53 failed");
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }

    xTaskCreate(dns_task, "captive_dns", 3072, NULL, 5, &s_task);
    ESP_LOGI(TAG, "Captive DNS started");
    return ESP_OK;
}

void captive_dns_stop(void)
{
    s_stop = true;
    if (s_sock >= 0) {
        shutdown(s_sock, SHUT_RDWR);
    }
}
