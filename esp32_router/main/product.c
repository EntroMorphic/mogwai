/* product.c — the router as an actual device.
 *
 * Utterance in over UART, GPIO out. This is the firmware that makes the result
 * a thing rather than a benchmark: main.c proves the arithmetic matches the
 * host, this proves the arithmetic drives a pin.
 *
 * The safety property measured throughout this project is enforced here in one
 * place: **nothing actuates unless the router accepts.** Held-out, on the
 * SHIPPED 240 KB index, that is 12 false actuations in 2754 non-commands
 * (0.44%) — test evaluation #6. A rejected utterance leaves every output
 * exactly as it was.
 *
 * Pins. Overridable at build time:
 *     idf.py -DPRODUCT=1 -DPIN_LIGHT_NUM=2 -DPIN_CLEANING_NUM=25 ... build
 *
 * WROVER WARNING. The defaults below use GPIO16 and GPIO17, which on any
 * WROVER-class module (ESP32-D0WD *with PSRAM*) are the PSRAM chip-select and
 * clock lines — IDF's own defaults are D0WD_PSRAM_CS_IO=16, CLK_IO=17. The
 * board this was developed on is a plain WROOM with no PSRAM, so the collision
 * never appeared. On a WROVER, MOVE THESE PINS. GPIO25/26/27 are safe choices
 * on both.
 *
 * GPIO2 is a strapping pin: it must be low or floating at boot for the ROM
 * download mode, which is why it is only driven after boot. It is also the
 * onboard LED on most devkits, which makes the light channel visible with no
 * wiring.
 *
 *   GPIO2   light      LEDC PWM, so on/off/up/dim/change all drive one channel
 *   GPIO4   wemo       plain level
 *   GPIO16  cleaning   250 ms pulse   <- PSRAM CS on WROVER
 *   GPIO17  coffee     250 ms pulse   <- PSRAM CLK on WROVER
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "router.h"
#include "ternary.h"
#include "lift.h"

#ifndef PIN_LIGHT_NUM
#define PIN_LIGHT_NUM     2
#endif
#ifndef PIN_WEMO_NUM
#define PIN_WEMO_NUM      4
#endif
#ifndef PIN_CLEANING_NUM
#define PIN_CLEANING_NUM 16
#endif
#ifndef PIN_COFFEE_NUM
#define PIN_COFFEE_NUM   17
#endif
#define PIN_LIGHT     ((gpio_num_t)PIN_LIGHT_NUM)
#define PIN_WEMO      ((gpio_num_t)PIN_WEMO_NUM)
#define PIN_CLEANING  ((gpio_num_t)PIN_CLEANING_NUM)
#define PIN_COFFEE    ((gpio_num_t)PIN_COFFEE_NUM)
#define LEDC_CH       LEDC_CHANNEL_0
#define LEDC_TIM      LEDC_TIMER_0
#define DUTY_MAX      255           /* 8-bit resolution: integer, no float */
#define DUTY_STEP     64            /* up/dim move a quarter of full scale */

extern const uint8_t blob_start[] asm("_binary_router_bin_start");
extern const uint8_t blob_end[]   asm("_binary_router_bin_end");

static router_t R;
static const tvec *TI;
static const uint16_t *ACT;

/* The index is scanned strictly in order, so it does not need to be one
   allocation. esp_get_free_heap_size() reports the SUM across heap regions, but
   the ESP32 splits DRAM into non-contiguous blocks, so a single malloc is
   bounded by the LARGEST block. Chunked, it uses nearly all of it and then
   spills into the IRAM-only pool that malloc cannot reach at all.
   The policy lives in lift.c and is shared with wifiprobe.c, so the arrangement
   that is measured and the arrangement that ships are the same code. */
static lift_t L;                             /* reported in the banner: a printf
   here would sit in stdout's buffer and be lost when uart_param_config
   reconfigures the console */
static const uint8_t *REFP;
static uint32_t NREF;

/* ---- device state, integer only ------------------------------------------ */
static int light_duty = 0;      /* 0..DUTY_MAX */
static int wemo_on    = 0;

static void light_set(int duty) {
    if (duty < 0) duty = 0;
    if (duty > DUTY_MAX) duty = DUTY_MAX;
    light_duty = duty;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CH, (uint32_t)duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CH);
}
static void pulse(gpio_num_t pin) {
    gpio_set_level(pin, 1);
    vTaskDelay(pdMS_TO_TICKS(250));
    gpio_set_level(pin, 0);
}

static void io_init(void) {
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_WEMO) | (1ULL << PIN_CLEANING) | (1ULL << PIN_COFFEE),
        .mode = GPIO_MODE_OUTPUT, .pull_up_en = 0, .pull_down_en = 0, .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(PIN_WEMO, 0);
    gpio_set_level(PIN_CLEANING, 0);
    gpio_set_level(PIN_COFFEE, 0);

    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIM,
        .duty_resolution = LEDC_TIMER_8_BIT, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&t);
    ledc_channel_config_t c = {
        .gpio_num = PIN_LIGHT, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CH,
        .timer_sel = LEDC_TIM, .duty = 0, .hpoint = 0,
    };
    ledc_channel_config(&c);
    light_set(0);
}

/* ---- the actuation table ------------------------------------------------- */
/* Returns a human-readable description of what was DONE, or NULL if the intent
   is recognised but has no wired effect on this board. */
static const char *actuate(const char *cls) {
    static char msg[96];
    if (!strcmp(cls, "iot_hue_lighton"))   { light_set(DUTY_MAX); snprintf(msg,sizeof msg,"light -> ON  (duty %d/%d)", light_duty, DUTY_MAX); return msg; }
    if (!strcmp(cls, "iot_hue_lightoff"))  { light_set(0);        snprintf(msg,sizeof msg,"light -> OFF (duty %d/%d)", light_duty, DUTY_MAX); return msg; }
    if (!strcmp(cls, "iot_hue_lightup"))   { light_set(light_duty + DUTY_STEP); snprintf(msg,sizeof msg,"light -> UP  (duty %d/%d)", light_duty, DUTY_MAX); return msg; }
    if (!strcmp(cls, "iot_hue_lightdim"))  { light_set(light_duty - DUTY_STEP); snprintf(msg,sizeof msg,"light -> DIM (duty %d/%d)", light_duty, DUTY_MAX); return msg; }
    if (!strcmp(cls, "iot_hue_lightchange")){ light_set(light_duty ? light_duty : DUTY_MAX/2);
                                              snprintf(msg,sizeof msg,"light -> COLOUR change (no RGB wired; duty %d)", light_duty); return msg; }
    if (!strcmp(cls, "iot_wemo_on"))       { wemo_on = 1; gpio_set_level(PIN_WEMO, 1); return "wemo -> ON  (GPIO4 high)"; }
    if (!strcmp(cls, "iot_wemo_off"))      { wemo_on = 0; gpio_set_level(PIN_WEMO, 0); return "wemo -> OFF (GPIO4 low)"; }
    if (!strcmp(cls, "iot_cleaning"))      { pulse(PIN_CLEANING); return "cleaner -> START (GPIO16 pulsed 250 ms)"; }
    if (!strcmp(cls, "iot_coffee"))        { pulse(PIN_COFFEE);   return "coffee  -> BREW  (GPIO17 pulsed 250 ms)"; }
    return NULL;
}


static int load(void) {
    const uint8_t *p = blob_start;
    uint32_t hdr[5]; memcpy(hdr, p, 20); p += 20;
    if (hdr[0] != RMAGIC || hdr[1] != RD) return -1;
    /* Validate the body against the blob's actual extent, not just the header.
     *
     * magic and dim survive a TRUNCATED blob - a partial flash write, an
     * interrupted `idf.py flash` - while n_index does not describe what is
     * really there. Without this the firmware would compute TI/ACT pointers
     * past the mapped region and lift_run would memcpy from unmapped flash.
     * blobfmt performs exactly this check offline; the firmware did not.
     *
     * Layout per doc/BLOB_FORMAT.md: 20 header + names + centre + n labels
     * + 2n act + 64n index + 4 nref. */
    {
        size_t have = (size_t)(blob_end - blob_start);
        size_t need = 20 + RNAMELEN * RMAXCLS + sizeof(int32_t) * RD
                    + (size_t)hdr[2] * (1 + 2 + sizeof(tvec)) + 4;
        if (hdr[2] == 0 || need > have) return -1;
    }
    R.magic=hdr[0]; R.dim=hdr[1]; R.n_index=hdr[2]; R.n_class=hdr[3]; R.threshold=(int32_t)hdr[4];
    memcpy(R.names, p, RNAMELEN * RMAXCLS); p += RNAMELEN * RMAXCLS;
    memcpy(R.centre, p, sizeof(int32_t) * RD); p += sizeof(int32_t) * RD;
    R.label = (uint8_t *)p; p += R.n_index;
    ACT = (const uint16_t *)p; p += (size_t)R.n_index * 2;
    TI = (const tvec *)p; p += (size_t)R.n_index * sizeof(tvec);
    memcpy(&NREF, p, 4); p += 4; REFP = p;

    /* Lift the index out of flash if it fits.
     *
     * Measured: identical code over identical vectors costs 4168 ns/vector from
     * flash-mapped memory and 1617 ns from internal SRAM - 2.58x, because the
     * cost is cache-MMU and SPI overhead, not flash bandwidth. Pure memory touch
     * is 20x cheaper. So if the index fits in heap, moving it there is the
     * single largest win available on this part.
     *
     * Adaptive rather than compile-time: try, and fall back to flash-mapped if
     * the allocation fails. A device that boots slower is better than one that
     * does not boot. */
#ifdef MOGWAI_WIFI
    return lift_run(&L, TI, ACT, R.n_index, LIFT_RESERVE_TLS);
#else
    return lift_run(&L, TI, ACT, R.n_index, LIFT_RESERVE_BARE);
#endif
}

static int route(const char *txt, int *score_out) {
    tvec q; t_encode(&R, txt, &q);
    int aa = t_active(&q), best = -(1 << 28); uint32_t bi = 0, i = 0;
    for (uint32_t c = 0; c < L.nch; c++) {
        const tvec *base = L.ch[c];
        uint32_t n = R.n_index - i; if (n > LIFT_CHUNK_VECS) n = LIFT_CHUNK_VECS;
        for (uint32_t k = 0; k < n; k++, i++) {
            int s = t_score_pre(&q, &base[k], aa, L.act[i]);
            if (s > best) { best = s; bi = i; }
        }
    }
    if (score_out) *score_out = best;
    if (best <= R.threshold) return -1;              /* REJECT: below threshold */
    int cls = r_apply_polarity(&R, R.label[bi], txt);
    /* The nearest stored utterance may itself be a NON-command. That is a
       rejection too, and a different one - the score cleared the bar but the
       match is not a command. Missing this made the firmware print ACTUATED
       for "what time does the train leave". Nothing moved, because the table
       has no none case, but reporting an actuation for a non-command is the
       one thing this system must never do. */
    if (!strcmp(R.names[cls], "none")) return -2;
    return cls;
}


#ifdef MOGWAI_WIFI
/* ---- optional network, MOGWAI_WIFI=1 -------------------------------------
 * Off by default and absent from the default build entirely. With it on, the
 * stack comes up and associates BEFORE the index is lifted, so the lift sees
 * the heap a connected product actually has - and the reserve switches to
 * LIFT_RESERVE_TLS, which is sized from a measured handshake rather than by eye.
 *
 * Status is stashed rather than printed: a printf here would sit in stdout's
 * buffer and be lost when uart_param_config reconfigures the console.
 */
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#if __has_include("wifi_creds.h")
#include "wifi_creds.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

static char wifi_status[64] = "not attempted";
static EventGroupHandle_t WEG;
static int wretry = 0;

static void wifi_ev(void *a, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) esp_wifi_connect();
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (++wretry <= 8) esp_wifi_connect(); else xEventGroupSetBits(WEG, BIT1);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        snprintf(wifi_status, sizeof wifi_status, "connected, IP " IPSTR,
                 IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(WEG, BIT0);
    }
}

static void wifi_up(void) {
    esp_log_level_set("*", ESP_LOG_WARN);
    if (WIFI_SSID[0] == '\0') {
        snprintf(wifi_status, sizeof wifi_status, "NO CREDENTIALS (wifi_creds.h)");
        return;
    }
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    WEG = xEventGroupCreate();
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_ev, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_ev, NULL, NULL);
    wifi_init_config_t wc = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wc);
    esp_wifi_set_mode(WIFI_MODE_STA);
    wifi_config_t sta = { 0 };
    snprintf((char *)sta.sta.ssid,     sizeof sta.sta.ssid,     "%s", WIFI_SSID);
    snprintf((char *)sta.sta.password, sizeof sta.sta.password, "%s", WIFI_PASS);
    esp_wifi_set_config(WIFI_IF_STA, &sta);
    esp_wifi_start();
    EventBits_t b = xEventGroupWaitBits(WEG, BIT0 | BIT1, pdFALSE, pdFALSE,
                                        pdMS_TO_TICKS(30000));
    if (!(b & BIT0)) snprintf(wifi_status, sizeof wifi_status,
                              "association FAILED after %d retries", wretry);
}

static esp_err_t tls_sink(esp_http_client_event_t *e) { (void)e; return ESP_OK; }

/* `!tls` at the prompt. The point is not the fetch, it is that a handshake and
 * a resident index coexist - and that routing still works afterwards. */
static void do_tls(void) {
    unsigned before = esp_get_free_heap_size();
    esp_http_client_config_t cfg = { .url = "https://www.howsmyssl.com/",
        .event_handler = tls_sink, .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) { printf("  TLS: client init failed\n"); return; }
    int64_t t0 = esp_timer_get_time();
    esp_err_t r = esp_http_client_perform(c);
    int64_t us = esp_timer_get_time() - t0;
    if (r == ESP_OK)
        printf("  TLS  HTTP %d, %d bytes in %lld us\n",
               esp_http_client_get_status_code(c),
               (int)esp_http_client_get_content_length(c), (long long)us);
    else
        printf("  TLS  FAILED: %s\n", esp_err_to_name(r));
    esp_http_client_cleanup(c);
    printf("  heap free %u -> %u   min-ever %u\n", before,
           (unsigned)esp_get_free_heap_size(),
           (unsigned)esp_get_minimum_free_heap_size());
}
#endif  /* MOGWAI_WIFI */
void app_main(void) {
    t_popcnt_init();
    io_init();
#ifdef MOGWAI_WIFI
    wifi_up();          /* before load(): the lift must see a connected heap */
#endif
    if (load()) { printf("BLOB PARSE FAILED\n"); return; }

    uart_config_t uc = { .baud_rate = 115200, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT };
    uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_0, &uc);
    /* The RX buffer holds boot-time line noise from the ROM loader and the
       DTR/RTS reset pulse. Without this the prompt reprints once per stray
       byte - several hundred times before the first real utterance. */
    vTaskDelay(pdMS_TO_TICKS(200));
    uart_flush_input(UART_NUM_0);

    printf("\n===== mogwai =====\n");
    printf("index %u vectors, threshold %d, %u classes\n",
           (unsigned)R.n_index, (int)R.threshold, (unsigned)R.n_class);
    printf("pins: light PWM=GPIO%d  wemo=GPIO%d  cleaning=GPIO%d  coffee=GPIO%d\n",
           PIN_LIGHT, PIN_WEMO, PIN_CLEANING, PIN_COFFEE);
    printf("index %u/%u vectors in SRAM (%u%%), %u chunks of %u\n",
           (unsigned)(L.vec_dram + L.vec_iram), (unsigned)R.n_index,
           (unsigned)(R.n_index ? (L.vec_dram + L.vec_iram) * 100 / R.n_index : 0),
           (unsigned)L.nch, (unsigned)LIFT_CHUNK_VECS);
    printf("  %u in DRAM, %u in the IRAM-only pool (malloc cannot reach it)\n",
           (unsigned)L.vec_dram, (unsigned)L.vec_iram);
    printf("  addressing verified over all %u vectors: %u MISMATCHED\n",
           (unsigned)R.n_index, (unsigned)L.bad);
    printf("  needs %u B | largest contiguous block %u B | total free %u B\n",
           (unsigned)L.need, (unsigned)L.largest, (unsigned)esp_get_free_heap_size());
#ifdef MOGWAI_WIFI
    printf("wifi: %s   (reserve %u B, sized from a measured TLS handshake)\n",
           wifi_status, (unsigned)LIFT_RESERVE_TLS);
    printf("type !tls to fetch over HTTPS and see the heap move\n");
#endif
    fflush(stdout);

    char line[256]; int n = 0;
    for (;;) {
        uint8_t ch;
        int got = uart_read_bytes(UART_NUM_0, &ch, 1, portMAX_DELAY);
        if (got != 1) continue;
        if (ch == '\r' || ch == '\n') {
            if (!n) continue;              /* bare newline: no prompt, no noise */;
            line[n] = 0;
#ifdef MOGWAI_WIFI
            if (!strcmp(line, "!tls")) { printf("\n"); do_tls(); n = 0; printf("\n> "); fflush(stdout); continue; }
#endif
            int score; int64_t t0 = esp_timer_get_time();
            int cls = route(line, &score);
            int64_t us = esp_timer_get_time() - t0;
            printf("\n  \"%s\"\n", line);
            if (cls == -1) {
                printf("  REJECTED     score %d <= threshold %d — no output changed\n",
                       score, (int)R.threshold);
            } else if (cls == -2) {
                printf("  REJECTED     nearest match is not a command — no output changed\n");
            } else {
                const char *what = actuate(R.names[cls]);
                printf("  %-12s score %d  (margin +%d)\n", R.names[cls], score, score - (int)R.threshold);
                printf("  ACTUATED     %s\n", what ? what : "(intent recognised, no pin wired)");
            }
            printf("  %lld us\n\n> ", us);
            fflush(stdout);
            n = 0;
        } else if (n < (int)sizeof(line) - 1 && ch >= 0x20) {
            line[n++] = (char)ch;
        }
    }
}
