/* wifiprobe.c — how much heap does a FULLY FUNCTIONAL WiFi stack actually need?
 *
 * Not shipped. This exists to answer one question with a number: if the index
 * is lifted into SRAM after WiFi is up, how much heap must be left in reserve
 * so the stack does not run out later?
 *
 * Every residency figure measured so far was taken with esp_wifi_start() called
 * but NOTHING ASSOCIATED — no AP, no DHCP, no sockets, no TLS. Those are
 * boot-time numbers and they are optimistic. A TLS handshake in particular
 * allocates tens of KB transiently and then frees it, so the figure that
 * governs the reserve is the MINIMUM-EVER free heap, not the steady state.
 *
 * Reports free heap at each stage, and the low-water mark across the whole run:
 *
 *     boot -> wifi_init -> wifi_start -> associated -> got IP -> TCP -> TLS
 *
 * Credentials come from wifi_creds.h, which is gitignored and must never be
 * committed. Copy wifi_creds.h.example and fill it in, or pass them on the
 * build command line.
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <stdlib.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_tls.h"
#include "router.h"
#include "ternary.h"
#include "lift.h"

#if __has_include("wifi_creds.h")
#include "wifi_creds.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

#define CONNECTED_BIT  BIT0
#define FAILED_BIT     BIT1

static EventGroupHandle_t EG;
static int retries = 0;
static int may_connect = 0;   /* scan first: the radio refuses a scan while connecting */

/* The low-water mark is the number that sets the reserve. esp_get_minimum_
   free_heap_size() is maintained by the allocator across the whole run, so it
   catches transient peaks that a sampled printf would miss entirely. */
static void stage(const char *label) {
    printf("  %-26s free %7u   largest %7u   min-ever %7u\n",
           label,
           (unsigned)esp_get_free_heap_size(),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
           (unsigned)esp_get_minimum_free_heap_size());
    fflush(stdout);
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (may_connect) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (may_connect && ++retries <= 8) { esp_wifi_connect(); }
        else { xEventGroupSetBits(EG, FAILED_BIT); }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        printf("  got IP " IPSTR "\n", IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(EG, CONNECTED_BIT);
    }
}


/* ---- lift the index, then prove TLS still works -------------------------- */
/* Mirrors product.c's load() by calling the SAME validating parser and the SAME
   lift_run(). The probe never routes, so it needs only the index geometry —
   enough to occupy memory exactly as the product would. If the probe and the
   product allocated differently, the measurement would be of nothing. */
extern const uint8_t blob_start[] asm("_binary_router_bin_start");
extern const uint8_t blob_end[]   asm("_binary_router_bin_end");
static router_t R;
static rindex2  IX;
static lift_t   L;

static void lift_the_index(void) {
    if (r_parse2(&R, &IX, blob_start, (size_t)(blob_end - blob_start))) {
        printf("  BLOB PARSE FAILED\n"); return; }

    lift_run(&L, IX.mask, IX.act, IX.eoff, IX.epos, IX.nex,
             R.n_index, LIFT_RESERVE_TLS);
    printf("  index %u/%u in SRAM (%u%%): %u DRAM + %u IRAM-only, %u MISMATCHED\n",
           (unsigned)(L.vec_dram + L.vec_iram), (unsigned)R.n_index,
           (unsigned)(R.n_index ? (L.vec_dram + L.vec_iram) * 100 / R.n_index : 0),
           (unsigned)L.vec_dram, (unsigned)L.vec_iram, (unsigned)L.bad);
}

static esp_err_t sink(esp_http_client_event_t *e) { (void)e; return ESP_OK; }

/* One request. `tls` picks https (mbedTLS handshake) vs plain http. */
static void fetch(const char *url, int tls, const char *label) {
    esp_http_client_config_t cfg = {
        .url = url, .event_handler = sink, .timeout_ms = 15000,
        .crt_bundle_attach = tls ? esp_crt_bundle_attach : NULL,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) { printf("  %-26s client init FAILED\n", label); return; }
    esp_err_t r = esp_http_client_perform(c);
    if (r == ESP_OK) {
        printf("  %-26s HTTP %d, %d bytes\n", label,
               esp_http_client_get_status_code(c),
               (int)esp_http_client_get_content_length(c));
    } else {
        printf("  %-26s FAILED: %s\n", label, esp_err_to_name(r));
    }
    esp_http_client_cleanup(c);
    stage(label);
}


/* A HELD-OPEN TLS session, which is what a one-connection device actually has.
 *
 * Every TLS measurement before this was connect-then-CLOSE, so it captured the
 * handshake spike and the residue after teardown — not what a live session
 * retains while it is up. mbedTLS holds record buffers for the life of the
 * connection, sized by MBEDTLS_SSL_IN/OUT_CONTENT_LEN, and IDF defaults those
 * to full 16 KB TLS records. If that is held continuously it comes straight off
 * the headroom the lift reserve was sized against. */

/* Is the SSID even on the air? Association failing and the AP being switched
 * off look identical from the retry counter, so ask the radio directly before
 * blaming the code. */
static void scan_for_ssid(void) {
    wifi_scan_config_t sc = { 0 };
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) { printf("  scan failed\n"); return; }
    uint16_t n = 0; esp_wifi_scan_get_ap_num(&n);
    if (n == 0) { printf("  scan: NO access points visible at all\n"); return; }
    wifi_ap_record_t *ap = calloc(n, sizeof *ap);
    if (!ap) { printf("  scan: out of memory\n"); return; }
    esp_wifi_scan_get_ap_records(&n, ap);
    int found = 0;
    printf("  scan: %u AP(s) visible\n", (unsigned)n);
    for (uint16_t i = 0; i < n; i++) {
        if (!strcmp((char *)ap[i].ssid, WIFI_SSID)) {
            printf("  scan: TARGET \"%s\" present, rssi %d, ch %u\n",
                   ap[i].ssid, ap[i].rssi, (unsigned)ap[i].primary);
            found = 1;
        }
    }
    if (!found) printf("  scan: TARGET \"%s\" NOT PRESENT — the AP is off or out of range\n",
                       WIFI_SSID);
    free(ap);
}
static void hold_tls_open(void) {
    const char *host = "www.howsmyssl.com";
    printf("  MBEDTLS_SSL_IN_CONTENT_LEN=%d  OUT=%d\n",
           CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN, CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN);
    unsigned before = esp_get_free_heap_size();
    esp_tls_cfg_t cfg = { .crt_bundle_attach = esp_crt_bundle_attach };
    esp_tls_t *tls = esp_tls_init();
    if (!tls) { printf("  esp_tls_init FAILED\n"); return; }
    if (esp_tls_conn_new_sync(host, strlen(host), 443, &cfg, tls) != 1) {
        printf("  held TLS: connect FAILED\n"); esp_tls_conn_destroy(tls); return;
    }
    unsigned after_hs = esp_get_free_heap_size();
    char req[160];
    int rl = snprintf(req, sizeof req,
                      "GET / HTTP/1.1\r\nHost: %s\r\nConnection: keep-alive\r\n\r\n", host);
    esp_tls_conn_write(tls, req, rl);
    char buf[512]; int total = 0, n;
    do { n = esp_tls_conn_read(tls, buf, sizeof buf); if (n > 0) total += n; }
    while (n > 0 && total < 2000);
    unsigned held = esp_get_free_heap_size();
    printf("  session OPEN: %d bytes read\n", total);
    printf("  free %u -> %u after handshake -> %u with session held\n",
           before, after_hs, held);
    printf("  HELD SESSION COSTS %d B (steady, while open)\n", (int)before - (int)held);
    vTaskDelay(pdMS_TO_TICKS(3000));
    printf("  after 3 s still held: free %u   min-ever %u\n",
           (unsigned)esp_get_free_heap_size(),
           (unsigned)esp_get_minimum_free_heap_size());
    esp_tls_conn_destroy(tls);
    printf("  after close:          free %u   min-ever %u\n",
           (unsigned)esp_get_free_heap_size(),
           (unsigned)esp_get_minimum_free_heap_size());
}
void app_main(void) {
    esp_log_level_set("*", ESP_LOG_WARN);   /* the log itself allocates; quiet it */
    vTaskDelay(pdMS_TO_TICKS(500));
    printf("\n===== wifiprobe: heap cost of a functional WiFi stack =====\n");

    if (WIFI_SSID[0] == '\0') {
        printf("\n  NO CREDENTIALS.\n"
               "  Copy esp32_router/main/wifi_creds.h.example to wifi_creds.h and\n"
               "  fill it in. That file is gitignored and must never be committed.\n"
               "  Measuring the unassociated case only.\n\n");
    }
    stage("boot");

    nvs_flash_init();                       stage("nvs_flash_init");
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();    stage("netif + lwIP");

    EG = xEventGroupCreate();
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_event, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_event, NULL, NULL);

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wcfg);                   stage("esp_wifi_init");
    esp_wifi_set_mode(WIFI_MODE_STA);

    if (WIFI_SSID[0]) {
        wifi_config_t sta = { 0 };
        snprintf((char *)sta.sta.ssid,     sizeof sta.sta.ssid,     "%s", WIFI_SSID);
        snprintf((char *)sta.sta.password, sizeof sta.sta.password, "%s", WIFI_PASS);
        esp_wifi_set_config(WIFI_IF_STA, &sta);
    }
    esp_wifi_start();                       stage("esp_wifi_start");
    if (WIFI_SSID[0]) { scan_for_ssid(); may_connect = 1; esp_wifi_connect(); }

    if (WIFI_SSID[0]) {
        EventBits_t b = xEventGroupWaitBits(EG, CONNECTED_BIT | FAILED_BIT,
                                            pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
        if (b & CONNECTED_BIT) {
            stage("associated + DHCP");
            lift_the_index();
            stage("index lifted");
            fetch("http://neverssl.com/",        0, "plain TCP + HTTP");
            fetch("https://www.howsmyssl.com/",  1, "TLS handshake + HTTPS");
            fetch("https://www.howsmyssl.com/",  1, "second TLS (warm)");
            printf("\n  ---- a HELD-OPEN session, not connect-then-close ----\n");
            hold_tls_open();
            stage("after held session closed");
        } else {
            printf("  association FAILED or timed out after %d retries\n", retries);
        }
    }

    printf("\n  ---- what this means for the index lift ----\n");
    unsigned now = esp_get_free_heap_size(), lo = esp_get_minimum_free_heap_size();
    printf("  steady-state free after everything : %u B\n", now);
    printf("  minimum free ever seen             : %u B\n", lo);
    printf("  transient depth (steady - min)     : %d B\n", (int)now - (int)lo);
    printf("\n  The reserve must cover the TRANSIENT DEPTH plus a margin, not the\n"
           "  steady state. Lifting the index down to the steady-state figure would\n"
           "  fail on the next TLS handshake.\n");
    printf("  index chunks that fit in what is left: %u of 30 (8192 B each)\n",
           (unsigned)(lo > 8192 ? (lo - 8192) / 8192 : 0));
    fflush(stdout);
}
