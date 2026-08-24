/* powerprobe.c — hold the board in defined, sustained states so an inline USB
 * power meter can be read against each one.
 *
 * WHY A SEPARATE FIRMWARE. The ESP32 has no current sensor, so the measurement
 * is external and manual: you watch a meter. That makes the *protocol* the
 * whole experiment, and three things will silently corrupt it if the firmware
 * is not built for the job.
 *
 *   1. A USB meter reads the WHOLE BOARD — regulator, CP2102/CH340 bridge,
 *      LEDs, everything. Absolute readings are not the scan's power and never
 *      will be. Only the DELTA between two states isolates anything, which is
 *      why every state below differs from its neighbour in exactly one way.
 *
 *   2. Serial output costs power. The UART and the USB bridge both draw while
 *      transmitting, and it is easily milliamps. A state that prints and a
 *      state that does not are not comparable. So each state announces itself,
 *      drains the UART, and then goes SILENT for the whole hold.
 *
 *   3. PIN_LIGHT is GPIO2 — the onboard LED on most devkits — driven by LEDC
 *      PWM. If a routed utterance actuated during a hold, the LED duty would
 *      change the current directly and be read as scan power. This probe
 *      routes and DISCARDS. No GPIO moves, no PWM channel is started, and the
 *      pins are parked at a fixed level for every state alike.
 *
 * The scan states use the same blob, the same r_parse2, and the same lift_run
 * with the same reserve as the product, so the memory arrangement being
 * measured is the one that ships. */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_sleep.h"
#include "router.h"
#include "ternary.h"
#include "lift.h"

#ifndef HOLD_S
#define HOLD_S 30                    /* seconds per state; USB meters average
                                        over ~1 s, so give them room to settle */
#endif

extern const uint8_t blob_start[] asm("_binary_router_bin_start");
extern const uint8_t blob_end[]   asm("_binary_router_bin_end");

static router_t R;
static rindex2  IX;
static lift_t   L;

/* A representative query. Fixed, so every scan state does identical work. */
static const char *QTEXT = "turn off the light in the bathroom";

static tvec    QV;
static uint8_t QE[RD];
static int     QN, QA;
static volatile int SINK;

/* One full scan, actuating nothing. Returns the winning score so the compiler
   cannot discard the work. */
static int scan_once(void) {
    int best = -(1 << 28); uint32_t i = 0;
    for (uint32_t c = 0; c < L.nch; c++) {
        const uint32_t *base = L.ch[c];
        uint32_t n = R.n_index - i; if (n > LIFT_CHUNK_VECS) n = LIFT_CHUNK_VECS;
        for (uint32_t k = 0; k < n; k++, i++) {
            int s = t_score_ex(QV.m, QE, QN, base + (size_t)k * RWORDS,
                               L.epos + L.eoff[i],
                               (int)(L.eoff[i + 1] - L.eoff[i]), QA, L.act[i]);
            if (s > best) best = s;
        }
    }
    return best;
}

/* Announce, drain the UART, then hold in silence. `work` is 0 for idle,
   1 for back-to-back scanning, or a period in ms for duty-cycled scanning. */
static void hold(const char *name, const char *what, int period_ms) {
    printf("\n  STATE %-10s %s\n  holding %d s — read the meter now, then wait for the next STATE line\n",
           name, what, HOLD_S);
    fflush(stdout);
    /* NOT uart_wait_tx_done(): the console here is the VFS/ROM UART with no
       driver installed, so that call fails AND logs an error — inside the very
       window this protocol requires to be silent. fflush plus a delay is
       sufficient: the announce is ~130 chars, which is ~11 ms at 115200 baud. */
    vTaskDelay(pdMS_TO_TICKS(400));

    int64_t end = esp_timer_get_time() + (int64_t)HOLD_S * 1000000;
    uint32_t scans = 0;
    int64_t busy = 0;
    if (period_ms < 0) {                     /* idle: nothing but the tick */
        while (esp_timer_get_time() < end) vTaskDelay(pdMS_TO_TICKS(100));
    } else if (period_ms == 0) {             /* 100% duty */
        while (esp_timer_get_time() < end) {
            int64_t t0 = esp_timer_get_time();
            SINK = scan_once(); scans++;
            busy += esp_timer_get_time() - t0;
        }
    } else {                                 /* one scan per period */
        while (esp_timer_get_time() < end) {
            int64_t t0 = esp_timer_get_time();
            SINK = scan_once(); scans++;
            busy += esp_timer_get_time() - t0;
            int64_t left = period_ms * 1000 - (esp_timer_get_time() - t0);
            if (left > 0) vTaskDelay(pdMS_TO_TICKS(left / 1000));
        }
    }
    if (scans)
        printf("    (%u scans, %lld us busy of %d s = %.1f%% duty, %lld us/scan)\n",
               (unsigned)scans, busy, HOLD_S,
               100.0 * busy / ((double)HOLD_S * 1000000), busy / scans);
    fflush(stdout);
}

/* Light sleep between queries. THIS is where the power is.
 *
 * Measured: the scan costs 23 mA of a 71 mA total, and at 1 Hz it contributes
 * 0.5 mW against a 246 mW baseline. The router is 0.2% of the budget. So the
 * only measurement left that can matter is what the OTHER 99.8% does when the
 * part is allowed to sleep between utterances.
 *
 * Light sleep, not deep: deep sleep loses SRAM, and the whole point of this
 * design is a 137 KB index resident in SRAM. Reloading it from flash on every
 * wake would cost far more than it saved. Light sleep retains RAM. */
static void hold_sleep(const char *name, const char *what, int period_ms, int scan) {
    printf("\n  STATE %-10s %s\n  holding %d s — read the meter now, then wait for the next STATE line\n",
           name, what, HOLD_S);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(400));

    int64_t end = esp_timer_get_time() + (int64_t)HOLD_S * 1000000;
    uint32_t wakes = 0; int64_t busy = 0;
    while (esp_timer_get_time() < end) {
        int64_t t0 = esp_timer_get_time();
        if (scan) { SINK = scan_once(); busy += esp_timer_get_time() - t0; }
        wakes++;
        int64_t used = esp_timer_get_time() - t0;
        int64_t left = (int64_t)period_ms * 1000 - used;
        if (left > 0) {
            esp_sleep_enable_timer_wakeup((uint64_t)left);
            esp_light_sleep_start();
        }
    }
    printf("    (%u wakes, %lld us busy of %d s = %.2f%% awake-for-work)\n",
           (unsigned)wakes, busy, HOLD_S,
           100.0 * busy / ((double)HOLD_S * 1000000));
    fflush(stdout);
}

void app_main(void) {
    /* Park the actuator pins at a FIXED level and never touch them again. LEDC
       is deliberately not started: a running PWM channel draws, and a changing
       duty would be read as scan power. */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << 2) | (1ULL << 4) | (1ULL << 16) | (1ULL << 17),
        .mode = GPIO_MODE_OUTPUT, .pull_up_en = 0, .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(2, 0); gpio_set_level(4, 0);
    gpio_set_level(16, 0); gpio_set_level(17, 0);

    t_popcnt_init();
    printf("\n===== mogwai power probe =====\n");
    int rc = r_parse2(&R, &IX, blob_start, (size_t)(blob_end - blob_start));
    if (rc) { printf("  BLOB PARSE FAILED (%d)\n", rc); return; }
    lift_run(&L, IX.mask, IX.act, IX.eoff, IX.epos, IX.nex,
             R.n_index, LIFT_RESERVE_BARE);
    printf("  index %u/%u in SRAM (%u%%), %u chunks — %u MISMATCHED\n",
           (unsigned)(L.vec_dram + L.vec_iram), (unsigned)R.n_index,
           (unsigned)(R.n_index ? (L.vec_dram + L.vec_iram) * 100 / R.n_index : 0),
           (unsigned)L.nch, (unsigned)L.bad);

    t_encode(&R, QTEXT, &QV);
    QN = t_exceptions(&QV, QE);
    QA = t_active(&QV);

    printf("\n  Inline USB meter reads the WHOLE board, so absolute values are\n"
           "  not scan power. Record each state, then take DIFFERENCES:\n"
           "    scan energy per query = (I_scan100 - I_idle) * V * t_scan\n"
           "    always-on average     = I_idle + (I_scan100 - I_idle) * duty\n");

    for (;;) {
        hold("idle",    "CPU idle, no scanning, radio off", -1);
        hold("scan100", "scanning back-to-back, 100% duty", 0);
        hold("duty1hz", "one scan per second",              1000);
        hold("duty10hz","ten scans per second",             100);
        hold_sleep("sleepidle", "LIGHT SLEEP, waking 1/s, no scan",  1000, 0);
        hold_sleep("sleep1hz",  "LIGHT SLEEP, waking 1/s AND scanning", 1000, 1);
        printf("\n  --- cycle complete, repeating so you can re-read any state ---\n");
    }
}
