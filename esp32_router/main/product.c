/* product.c — the router as an actual device.
 *
 * Utterance in over UART, GPIO out. This is the firmware that makes the result
 * a thing rather than a benchmark: main.c proves the arithmetic matches the
 * host, this proves the arithmetic drives a pin.
 *
 * The safety property measured throughout this project is enforced here in one
 * place: **nothing actuates unless the router accepts.** Held-out, that is 8
 * false actuations in 2754 non-commands (0.29%). A rejected utterance leaves
 * every output exactly as it was.
 *
 * Pins (ESP32 devkit defaults; strapping pins avoided except GPIO2, which is
 * the onboard LED on most boards and is only driven after boot):
 *   GPIO2   light      LEDC PWM, so on/off/up/dim/change all drive one channel
 *   GPIO4   wemo       plain level
 *   GPIO16  cleaning   250 ms pulse
 *   GPIO17  coffee     250 ms pulse
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

#define PIN_LIGHT     GPIO_NUM_2
#define PIN_WEMO      GPIO_NUM_4
#define PIN_CLEANING  GPIO_NUM_16
#define PIN_COFFEE    GPIO_NUM_17
#define LEDC_CH       LEDC_CHANNEL_0
#define LEDC_TIM      LEDC_TIMER_0
#define DUTY_MAX      255           /* 8-bit resolution: integer, no float */
#define DUTY_STEP     64            /* up/dim move a quarter of full scale */

extern const uint8_t blob_start[] asm("_binary_router_bin_start");

static router_t R;
static const tvec *TI;
static const uint16_t *ACT;

/* The index is scanned strictly in order, so it does not need to be one
   allocation. That matters: esp_get_free_heap_size() reports the SUM across
   heap regions, but the ESP32 splits DRAM into several non-contiguous blocks,
   so a single malloc is bounded by the LARGEST block. Measured on this part:
   258,720 B needed, 295,764 B free in total, 163,840 B in the largest block.
   A flat lift can never succeed; a chunked one uses nearly all of it. */
#define CHUNK_VECS    128                    /* 8 KB per chunk: fits the small regions too */
#define MAXCHUNK      512                    /* 65,536 vectors; the 656 KB index is 10,500 */
#define SRAM_RESERVE  40960                  /* heap left for the rest of the firmware */
static const tvec *CH[MAXCHUNK];             /* chunk c -> SRAM copy, or into flash */
static uint32_t NCH;
static uint32_t vec_in_sram = 0, sram_bad = 0;
static size_t sram_need = 0, sram_largest = 0;   /* reported in the banner: a
   printf here would sit in stdout's buffer and be lost when uart_param_config
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

/* Copy as much of the index into SRAM as the heap will take, chunk by chunk.
 *
 * Measured: identical code over identical vectors costs 4168 ns/vector from
 * flash-mapped memory and 1617 ns from internal SRAM - 2.58x, because the cost
 * is cache-MMU and SPI overhead, not flash bandwidth. Pure memory touch is 20x
 * cheaper. So every chunk that lands in SRAM is a chunk scanned 2.58x faster.
 *
 * Partial is fine and is the point of chunking: a chunk that will not fit stays
 * flash-mapped and scores bit-identically, because it is the same bytes. The
 * device degrades to the old speed instead of failing to boot. */
static int lift_index(void)
{
    if (R.n_index > (uint32_t)MAXCHUNK * CHUNK_VECS) return -1;
    NCH = (R.n_index + CHUNK_VECS - 1) / CHUNK_VECS;
    sram_need    = (size_t)R.n_index * sizeof(tvec) + (size_t)R.n_index * 2;
    sram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

    /* ACT first: 2 B/vector, and it is touched once per vector like the index
       is. Small enough that it fits a region whatever else happens. */
    size_t abytes = (size_t)R.n_index * 2;
    if (heap_caps_get_free_size(MALLOC_CAP_8BIT) > abytes + SRAM_RESERVE) {
        uint16_t *ra = malloc(abytes);
        if (ra) { memcpy(ra, ACT, abytes); ACT = ra; }
    }

    for (uint32_t c = 0; c < NCH; c++) {
        uint32_t off = c * CHUNK_VECS;
        uint32_t n   = R.n_index - off; if (n > CHUNK_VECS) n = CHUNK_VECS;
        size_t   b   = (size_t)n * sizeof(tvec);
        CH[c] = TI + off;                            /* flash by default */
        if (heap_caps_get_free_size(MALLOC_CAP_8BIT) < b + SRAM_RESERVE) continue;
        tvec *dst = malloc(b);
        if (!dst) continue;                          /* no region big enough; try the next */
        memcpy(dst, TI + off, b);
        CH[c] = dst;
        vec_in_sram += n;
    }

    /* Verify the CHUNK ADDRESSING, not just the copy.
     *
     * memcmp'ing each chunk straight after memcpy'ing it is a check that cannot
     * fail - it re-reads what it just wrote. The real hazard in chunking is an
     * off-by-one at a chunk boundary, which would silently score vector i
     * against the bytes of some other vector. So walk every index through the
     * exact nested accessor the scan uses and compare it to the flat
     * flash-mapped original. Mutating CHUNK_VECS in either loop fires this. */
    {
        const tvec *flat = TI;
        uint32_t i = 0;
        for (uint32_t c = 0; c < NCH; c++) {
            const tvec *base = CH[c];
            uint32_t n = R.n_index - i; if (n > CHUNK_VECS) n = CHUNK_VECS;
            for (uint32_t k = 0; k < n; k++, i++)
                if (memcmp(&base[k], &flat[i], sizeof(tvec))) sram_bad++;
        }
        if (i != R.n_index) sram_bad++;          /* the walk must cover the index exactly */
    }
    return 0;
}

static int load(void) {
    const uint8_t *p = blob_start;
    uint32_t hdr[5]; memcpy(hdr, p, 20); p += 20;
    if (hdr[0] != RMAGIC || hdr[1] != RD) return -1;
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
    return lift_index();
}

static int route(const char *txt, int *score_out) {
    tvec q; t_encode(&R, txt, &q);
    int aa = t_active(&q), best = -(1 << 28); uint32_t bi = 0, i = 0;
    for (uint32_t c = 0; c < NCH; c++) {
        const tvec *base = CH[c];
        uint32_t n = R.n_index - i; if (n > CHUNK_VECS) n = CHUNK_VECS;
        for (uint32_t k = 0; k < n; k++, i++) {
            int s = t_score_pre(&q, &base[k], aa, ACT[i]);
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

void app_main(void) {
    t_popcnt_init();
    io_init();
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
           (unsigned)vec_in_sram, (unsigned)R.n_index,
           (unsigned)(R.n_index ? vec_in_sram * 100 / R.n_index : 0),
           (unsigned)NCH, (unsigned)CHUNK_VECS);
    printf("  addressing verified over all %u vectors: %u MISMATCHED\n",
           (unsigned)R.n_index, (unsigned)sram_bad);
    printf("  needs %u B | largest contiguous block %u B | total free %u B\n",
           (unsigned)sram_need, (unsigned)sram_largest, (unsigned)esp_get_free_heap_size());
    printf("type an utterance and press enter. non-commands actuate NOTHING.\n\n> ");
    fflush(stdout);

    char line[256]; int n = 0;
    for (;;) {
        uint8_t ch;
        int got = uart_read_bytes(UART_NUM_0, &ch, 1, portMAX_DELAY);
        if (got != 1) continue;
        if (ch == '\r' || ch == '\n') {
            if (!n) continue;              /* bare newline: no prompt, no noise */;
            line[n] = 0;
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
