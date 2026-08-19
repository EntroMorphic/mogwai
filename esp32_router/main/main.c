/* main.c — validate the twin-ternary router on real hardware.
 * Proves PARITY against host-computed references, not plausibility.
 * Zero float on the hot path; int32_t is the widest type. */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "ternary.h"

extern const uint8_t blob_start[] asm("_binary_router_bin_start");
extern const uint8_t blob_end[]   asm("_binary_router_bin_end");

static router_t R;
static const tvec *TI;
static uint32_t NREF; static const uint8_t *REFP;
static const uint16_t *ACT;   /* precomputed t_active per index vector */

static int load(void) {
    const uint8_t *p = blob_start;
    uint32_t hdr[5]; memcpy(hdr, p, 20); p += 20;
    if (hdr[0] != RMAGIC || hdr[1] != RD) return -1;
    R.magic = hdr[0]; R.dim = hdr[1]; R.n_index = hdr[2];
    R.n_class = hdr[3]; R.threshold = (int32_t)hdr[4];
    memcpy(R.names, p, RNAMELEN * RMAXCLS); p += RNAMELEN * RMAXCLS;
    memcpy(R.centre, p, sizeof(int32_t) * RD); p += sizeof(int32_t) * RD;
    R.label = (uint8_t *)p; p += R.n_index;
    ACT = (const uint16_t *)p; p += (size_t)R.n_index * 2;
    TI = (const tvec *)p; p += (size_t)R.n_index * sizeof(tvec);
    memcpy(&NREF, p, 4); p += 4;
    REFP = p;
    return 0;
}
/* the whole router: encode, scan, polarity. No allocation, no float. */
static int route(const char *txt, int *score_out) {
    tvec q; t_encode(&R, txt, &q);
    int aa = t_active(&q);
    int best = -(1 << 28); uint32_t bi = 0;
    for (uint32_t i = 0; i < R.n_index; i++) {
        int s = t_score_pre(&q, &TI[i], aa, ACT[i]);
        if (s > best) { best = s; bi = i; }
    }
    if (score_out) *score_out = best;
    if (best <= R.threshold) return -1;
    return r_apply_polarity(&R, R.label[bi], txt);
}
/* What does the RTOS actually cost in the hot loop? Time the identical scan
   three ways, using the raw cycle counter so the measurement does not depend
   on any IDF service. */
#include "xtensa/core-macros.h"
/* Where do 2,336 cycles per index vector actually go?
 * Separate memory bandwidth from arithmetic by running identical work over a
 * SMALL index that fits in the 32KB cache, versus the full 672KB one. */
static volatile int g_sink;

static uint32_t bench(const char *label, const tvec *q, int aa, uint32_t n, int mode) {
    uint32_t c0 = XTHAL_GET_CCOUNT(); int acc = 0;
    for (uint32_t i = 0; i < n; i++) {
        const tvec *b = &TI[i];
        switch (mode) {
        case 0: {   /* touch only: read all 64 bytes, no popcount */
            uint32_t s = 0;
            for (int w = 0; w < RWORDS; w++) s += b->m[w] ^ b->s[w];
            acc += (int)s; break; }
        case 1: {   /* t_dot only: the real popcounts, no normalisation */
            acc += t_dot(q, b); break; }
        case 2: {   /* t_dot + t_active(b): the redundant recompute */
            acc += t_dot(q, b) - t_active(b); break; }
        default:    /* full t_score: + the integer divide */
            acc += t_score(q, b, aa); break;
        }
    }
    uint32_t c1 = XTHAL_GET_CCOUNT(); g_sink = acc;
    printf("    %-34s %8.1f cycles/vector\n", label, (double)(c1 - c0) / n);
    return c1 - c0;
}
/* The one piece of real hardware going unused: core 1.
 * The scan is embarrassingly parallel — no shared state, no ordering. */
typedef struct { const tvec *q; int aa; uint32_t lo, hi; int best; uint32_t bi; } job_t;
static job_t g_job;
static TaskHandle_t g_worker, g_main;

static void scan_range(job_t *j) {
    int best = -(1 << 28); uint32_t bi = j->lo;
    for (uint32_t i = j->lo; i < j->hi; i++) {
        int s = t_score_pre(j->q, &TI[i], j->aa, ACT[i]);
        if (s > best) { best = s; bi = i; }
    }
    j->best = best; j->bi = bi;
}
static void worker_task(void *arg) {
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);   /* publish/consume the job */
        scan_range(&g_job);
        __atomic_thread_fence(__ATOMIC_RELEASE);
        xTaskNotifyGive(g_main);
    }
}
static int route_mt(const char *txt, int *score_out) {
    tvec q; t_encode(&R, txt, &q);
    int aa = t_active(&q);
    uint32_t mid = R.n_index / 2;
    g_job.q = &q; g_job.aa = aa; g_job.lo = mid; g_job.hi = R.n_index;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    xTaskNotifyGive(g_worker);
    job_t mine = { &q, aa, 0, mid, 0, 0 };
    scan_range(&mine);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    int best = mine.best; uint32_t bi = mine.bi;
    if (g_job.best > best) { best = g_job.best; bi = g_job.bi; }
    if (score_out) *score_out = best;
    if (best <= R.threshold) return -1;
    return r_apply_polarity(&R, R.label[bi], txt);
}
static void bench_mt(void) {
    g_main = xTaskGetCurrentTaskHandle();
    xTaskCreatePinnedToCore(worker_task, "scan1", 4096, NULL, 5, &g_worker, 1);
    const uint8_t *p = REFP; int agree = 0; int64_t tot1 = 0, tot2 = 0;
    for (uint32_t i = 0; i < NREF; i++) {
        uint8_t len = *p++; char txt[128];
        memcpy(txt, p, len); txt[len] = 0; p += len;
        int32_t hs; memcpy(&hs, p, 4); p += 4;
        int8_t hc = (int8_t)*p++;
        int s1, s2; int64_t t0;
        t0 = esp_timer_get_time(); int c1 = route(txt, &s1);    tot1 += esp_timer_get_time() - t0;
        t0 = esp_timer_get_time(); int c2 = route_mt(txt, &s2); tot2 += esp_timer_get_time() - t0;
        if (c1 == c2 && s1 == s2 && c2 == hc && s2 == hs) agree++;
    }
    printf("\n  --- handing the scan to the idle core ---\n");
    printf("    one core   : %lld us\n", tot1 / NREF);
    printf("    two cores  : %lld us   (%.2fx)\n", tot2 / NREF, (double)tot1 / tot2);
    printf("    parity     : %d/%lu  (device 2-core == device 1-core == host)\n",
           agree, (unsigned long)NREF);
}
/* ---- red-team the popcount result ----------------------------------------
 * The claim "16-bit table is identical on two cores, therefore flash-bound"
 * rested on 25.80 vs 25.85 ms with n=1 and no error bars. Two separate
 * problems: (a) no variance estimate, (b) a mechanism inferred from a null
 * result rather than tested. This measures both.
 * Integer only: min/median/max over repeats, never mean+-sd (needs sqrt). */
static int scan_n(const tvec *q, int aa, uint32_t n) {
    int best = -(1 << 28);
    for (uint32_t i = 0; i < n; i++) {
        int s = t_score_pre(q, &TI[i], aa, ACT[i]);
        if (s > best) best = s;
    }
    return best;
}
static int scan_n2(const tvec *q, int aa, uint32_t n) {
    uint32_t mid = n / 2;
    g_job.q = q; g_job.aa = aa; g_job.lo = mid; g_job.hi = n;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    xTaskNotifyGive(g_worker);
    job_t mine = { q, aa, 0, mid, 0, 0 };
    scan_range(&mine);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return g_job.best > mine.best ? g_job.best : mine.best;
}
#define NREP 15
static void isort(int64_t *v, int n) {
    for (int i = 1; i < n; i++) { int64_t k = v[i]; int j = i - 1;
        while (j >= 0 && v[j] > k) { v[j+1] = v[j]; j--; } v[j+1] = k; }
}
static void reps(const char *label, int cores, uint32_t n) {
    int64_t t[NREP];
    for (int r = 0; r < NREP; r++) {
        const uint8_t *p = REFP; int64_t acc = 0;
        for (uint32_t i = 0; i < NREF; i++) {
            uint8_t len = *p++; char txt[128];
            memcpy(txt, p, len); txt[len] = 0; p += len + 5;
            tvec q; t_encode(&R, txt, &q); int aa = t_active(&q);
            int64_t t0 = esp_timer_get_time();
            if (cores == 1) scan_n(&q, aa, n); else scan_n2(&q, aa, n);
            acc += esp_timer_get_time() - t0;
        }
        t[r] = acc / NREF;
    }
    isort(t, NREP);
    printf("    %-34s min %6lld  med %6lld  max %6lld us   (spread %lld)\n",
           label, t[0], t[NREP/2], t[NREP-1], t[NREP-1] - t[0]);
}
static void redteam(void) {
    uint32_t full = R.n_index, small = 400;   /* 400*64B = 25KB, fits 32KB cache */
    printf("\n  === RED-TEAM: %d repeats each, TPOPCNT=%d ===\n", NREP, TPOPCNT);
    printf("  -- full index (%lu vec, %lu KB, flash-resident) --\n",
           (unsigned long)full, (unsigned long)(full * sizeof(tvec) / 1024));
    reps("1 core", 1, full);
    reps("2 cores", 2, full);
    printf("  -- SMALL index (%lu vec, %lu KB, CACHE-resident) --\n",
           (unsigned long)small, (unsigned long)(small * sizeof(tvec) / 1024));
    reps("1 core", 1, small);
    reps("2 cores", 2, small);
    /* dispatch cost with zero work: is degraded scaling just sync overhead? */
    int64_t s[NREP]; tvec q; t_encode(&R, "x", &q); int aa = t_active(&q);
    for (int r = 0; r < NREP; r++) {
        int64_t t0 = esp_timer_get_time();
        for (int k = 0; k < 64; k++) scan_n2(&q, aa, 0);
        s[r] = (esp_timer_get_time() - t0) / 64;
    }
    isort(s, NREP);
    printf("  -- dual-core dispatch+join, zero work: med %lld us --\n", s[NREP/2]);
}
static void profile(const char *txt) {
    tvec q; t_encode(&R, txt, &q); int aa = t_active(&q);
    uint32_t small = 400;              /* 400 * 64B = 25KB, fits the 32KB cache */
    printf("\n  --- FULL index (%lu vectors, %lu KB — streams through a 32KB cache) ---\n",
           (unsigned long)R.n_index, (unsigned long)(R.n_index * sizeof(tvec) / 1024));
    bench("touch 64B only (no arithmetic)", &q, aa, R.n_index, 0);
    bench("+ popcount dot (t_dot)",          &q, aa, R.n_index, 1);
    bench("+ t_active(b) recompute",         &q, aa, R.n_index, 2);
    bench("+ integer divide (full t_score)", &q, aa, R.n_index, 3);
    printf("\n  --- SMALL index (%lu vectors, %lu KB — fits in cache) ---\n",
           (unsigned long)small, (unsigned long)(small * sizeof(tvec) / 1024));
    bench("touch 64B only (no arithmetic)", &q, aa, small, 0);
    bench("+ popcount dot (t_dot)",          &q, aa, small, 1);
    bench("+ t_active(b) recompute",         &q, aa, small, 2);
    bench("+ integer divide (full t_score)", &q, aa, small, 3);
    uint32_t e0 = XTHAL_GET_CCOUNT(); tvec z; t_encode(&R, txt, &z);
    uint32_t e1 = XTHAL_GET_CCOUNT();
    printf("\n    encode the query once            %8lu cycles (%.3f ms)\n",
           (unsigned long)(e1 - e0), (e1 - e0) / 240000.0);
}
static void rtos_tax(const char *txt) {
    int s; uint32_t c0, c1, normal, nosched, noint;
    c0 = XTHAL_GET_CCOUNT(); route(txt, &s); c1 = XTHAL_GET_CCOUNT();
    normal = c1 - c0;
    vTaskSuspendAll();
    c0 = XTHAL_GET_CCOUNT(); route(txt, &s); c1 = XTHAL_GET_CCOUNT();
    xTaskResumeAll();
    nosched = c1 - c0;
    portDISABLE_INTERRUPTS();
    c0 = XTHAL_GET_CCOUNT(); route(txt, &s); c1 = XTHAL_GET_CCOUNT();
    portENABLE_INTERRUPTS();
    noint = c1 - c0;
    printf("\n  --- what FreeRTOS costs in the scan ---\n");
    printf("    normal (task, ticks on) : %10lu cycles  %6.1f ms\n",
           (unsigned long)normal, normal / 240000.0);
    printf("    scheduler suspended     : %10lu cycles  %6.1f ms  (%+.2f%%)\n",
           (unsigned long)nosched, nosched / 240000.0, 100.0 * ((double)nosched - normal) / normal);
    printf("    interrupts disabled     : %10lu cycles  %6.1f ms  (%+.2f%%)\n",
           (unsigned long)noint, noint / 240000.0, 100.0 * ((double)noint - normal) / normal);
    printf("    -> cycles/index-vector  : %.1f\n", (double)noint / R.n_index);
}
void app_main(void) {
    t_popcnt_init();          /* no-op unless the 16-bit table is compiled in */
    esp_chip_info_t ci; esp_chip_info(&ci);
    printf("\n===== twin-ternary router on ESP32 =====\n");
    printf("chip        : %d core, rev %d\n", ci.cores, ci.revision);
    printf("free heap   : %lu bytes\n", (unsigned long)esp_get_free_heap_size());
    printf("blob        : %d bytes\n", (int)(blob_end - blob_start));
    if (load()) { printf("BLOB PARSE FAILED\n"); return; }
    printf("index       : %lu vectors, %lu classes, threshold %ld\n",
           (unsigned long)R.n_index, (unsigned long)R.n_class, (long)R.threshold);
    printf("resident    : %lu bytes of index (in flash, not RAM)\n",
           (unsigned long)(R.n_index * sizeof(tvec)));
    printf("free heap   : %lu bytes after parse\n", (unsigned long)esp_get_free_heap_size());

    const uint8_t *p = REFP;
    int agree_cls = 0, agree_score = 0; int64_t total_us = 0; int worst = 0;
    printf("\n  %-38s %-8s %-8s %s\n", "query", "host", "device", "us");
    for (uint32_t i = 0; i < NREF; i++) {
        uint8_t len = *p++; char txt[128];
        memcpy(txt, p, len); txt[len] = 0; p += len;
        int32_t hs; memcpy(&hs, p, 4); p += 4;
        int8_t hc = (int8_t)*p++;
        int ds; int64_t t0 = esp_timer_get_time();
        int dc = route(txt, &ds);
        int64_t us = esp_timer_get_time() - t0;
        total_us += us; if (us > worst) worst = (int)us;
        if (dc == hc) agree_cls++;
        if (ds == hs) agree_score++;
        if (i < 6)
            printf("  %-38s %-8d %-8d %lld%s\n", txt, hc, dc, us, dc == hc ? "" : "  <-- MISMATCH");
    }
    printf("\n  class  agreement : %lu/%lu\n", (unsigned long)agree_cls, (unsigned long)NREF);
    printf("  score  agreement : %lu/%lu  (exact integer match)\n",
           (unsigned long)agree_score, (unsigned long)NREF);
    printf("  latency          : %lld us mean, %d us worst\n", total_us / NREF, worst);
    printf("  free heap        : %lu bytes\n", (unsigned long)esp_get_free_heap_size());
    rtos_tax("turn off the light in the bathroom");
    profile("turn off the light in the bathroom");
    bench_mt();
    redteam();
    printf("\n===== %s =====\n",
           (agree_cls == (int)NREF && agree_score == (int)NREF) ? "PARITY EXACT" : "PARITY FAILED");
    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}
