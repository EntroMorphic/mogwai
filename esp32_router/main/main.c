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
static int64_t reps(const char *label, int cores, uint32_t n) {
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
    return t[NREP/2];
}
/* Is the 92% byte cost FLASH BANDWIDTH, or CACHE-MMU OVERHEAD?
 *
 * The earlier profile found "touch 64B only" costs the same cached or uncached
 * (646 vs 640 cycles/vector) — so reads from flash-mapped memory cost ~40
 * cycles each even on a cache HIT. That smells like MMU/bus overhead, not
 * flash latency. If so, the index living in internal SRAM would be far faster,
 * and a DMA prefetch scheme could reach it. If the cost is real flash
 * bandwidth, DMA buys nothing and this whole avenue is closed.
 *
 * Copy N vectors into DRAM and scan the same data both ways. */
static void dram_vs_flash(void) {
    const uint32_t N = 1800;                 /* 1800 * 64B = 115 KB, fits DRAM */
    tvec *D = malloc((size_t)N * sizeof(tvec));
    if (!D) { printf("  DRAM alloc failed\n"); return; }
    memcpy(D, TI, (size_t)N * sizeof(tvec));
    uint16_t *A = malloc(N * 2);
    memcpy(A, ACT, N * 2);
    printf("\n  === where does the byte cost actually live? ===\n");
    printf("    heap free: %u B, copied %u vectors (%u KB) into DRAM\n",
           (unsigned)esp_get_free_heap_size(), (unsigned)N, (unsigned)(N*64/1024));
    tvec q; t_encode(&R, "turn off the light in the bathroom", &q);
    int aa = t_active(&q);
    for (int rep = 0; rep < 3; rep++) {
        int64_t t0, tf, td; volatile int sink = 0;
        t0 = esp_timer_get_time();
        for (int r = 0; r < 4; r++) for (uint32_t i = 0; i < N; i++)
            sink += t_score_pre(&q, &TI[i], aa, ACT[i]);
        tf = esp_timer_get_time() - t0;
        t0 = esp_timer_get_time();
        for (int r = 0; r < 4; r++) for (uint32_t i = 0; i < N; i++)
            sink += t_score_pre(&q, &D[i], aa, A[i]);
        td = esp_timer_get_time() - t0;
        (void)sink;
        printf("    rep %d  flash-mapped %5lld ns/vec   DRAM %5lld ns/vec   speedup %.2fx\n",
               rep, tf * 1000 / (4 * N), td * 1000 / (4 * N), (double)tf / td);
    }
    /* and the pure-touch version, to separate memory from arithmetic */
    volatile uint32_t s = 0; int64_t t0;
    t0 = esp_timer_get_time();
    for (int r = 0; r < 4; r++) for (uint32_t i = 0; i < N; i++) s += TI[i].m[0] + TI[i].s[7];
    int64_t tf2 = esp_timer_get_time() - t0;
    t0 = esp_timer_get_time();
    for (int r = 0; r < 4; r++) for (uint32_t i = 0; i < N; i++) s += D[i].m[0] + D[i].s[7];
    int64_t td2 = esp_timer_get_time() - t0;
    printf("    touch-only   flash %5lld ns/vec   DRAM %5lld ns/vec   speedup %.2fx\n",
           tf2 * 1000 / (4 * N), td2 * 1000 / (4 * N), (double)tf2 / td2);
    free(D); free(A);
}
/* If per-vector flash reads cost ~40 cycles of MMU overhead each, BULK copies
 * should amortise it. That decides whether DMA double-buffering can work:
 * stream chunks flash->SRAM while computing on the previous chunk, so the CPU
 * always reads SRAM. Measure the bulk rate, then measure the pipelined scan. */
static void bulk_and_pipeline(void) {
    printf("\n  === can bulk transfer amortise the per-access overhead? ===\n");
    const uint32_t KB[] = { 4, 16, 64, 128 };
    for (int k = 0; k < 4; k++) {
        size_t n = KB[k] * 1024;
        void *dst = malloc(n);
        if (!dst) { printf("    %3u KB: alloc failed\n", (unsigned)KB[k]); continue; }
        int64_t t0 = esp_timer_get_time();
        for (int r = 0; r < 8; r++) memcpy(dst, (const void *)TI, n);
        int64_t dt = esp_timer_get_time() - t0;
        double mbs = (double)n * 8 / (dt / 1e6) / 1048576.0;
        printf("    memcpy %3u KB flash->DRAM: %6.2f MB/s  (%5lld ns per 64B vector)\n",
               (unsigned)KB[k], mbs, dt * 1000 / (8 * (int64_t)(n / 64)));
        free(dst);
    }
    /* Now the real thing: double-buffered scan. Copy chunk N+1 while scoring
       chunk N, so every score reads SRAM instead of flash-mapped memory. */
    const uint32_t CH = 512;                       /* 512 vectors = 32 KB per buffer */
    tvec *b0 = malloc(CH * sizeof(tvec)), *b1 = malloc(CH * sizeof(tvec));
    uint16_t *a0 = malloc(CH * 2), *a1 = malloc(CH * 2);
    if (!b0 || !b1 || !a0 || !a1) { printf("    pipeline alloc failed\n"); return; }
    tvec q; t_encode(&R, "turn off the light in the bathroom", &q);
    int aa = t_active(&q);
    int64_t t0 = esp_timer_get_time();
    int best = -(1 << 28); uint32_t bi = 0;
    tvec *cur = b0, *nxt = b1; uint16_t *ca = a0, *na = a1;
    uint32_t done = 0, first = CH < R.n_index ? CH : R.n_index;
    memcpy(cur, TI, first * sizeof(tvec)); memcpy(ca, ACT, first * 2);
    while (done < R.n_index) {
        uint32_t n = R.n_index - done; if (n > CH) n = CH;
        uint32_t ahead = R.n_index - (done + n); if (ahead > CH) ahead = CH;
        if (ahead) { memcpy(nxt, &TI[done + n], ahead * sizeof(tvec));
                     memcpy(na, &ACT[done + n], ahead * 2); }
        for (uint32_t i = 0; i < n; i++) {
            int s = t_score_pre(&q, &cur[i], aa, ca[i]);
            if (s > best) { best = s; bi = done + i; }
        }
        done += n;
        tvec *tb = cur; cur = nxt; nxt = tb;
        uint16_t *ta = ca; ca = na; na = ta;
    }
    int64_t dt = esp_timer_get_time() - t0;
    printf("\n    double-buffered scan (%u-vector chunks): %lld us   best=%d idx=%u\n",
           (unsigned)CH, dt, best, (unsigned)bi);
    printf("    vs straight flash-mapped scan          : 43498 us\n");
    printf("    -> %.2fx\n", 43498.0 / (double)dt);
    free(b0); free(b1); free(a0); free(a1);
}
/* The byte cost is not bandwidth we can accelerate — 656 KB at 24 MB/s is a
 * 27 ms floor no DMA or second core beats. But SRAM is ~20x cheaper per access
 * than flash-mapped memory, so the win is to NOT READ most of the index.
 *
 * Coarse-to-fine: keep a compact signature per vector in SRAM, scan those to
 * pick candidates, then touch flash only for the survivors.
 *
 * Signature: fold the 256-bit mask to 64 bits by OR-ing groups of 4 dims.
 * popcount(qf & bf) upper-bounds the true mask overlap, so it is a legitimate
 * ranking prior — it cannot under-estimate agreement. 10500 x 8B = 84 KB. */
static uint64_t *SIG64;
static uint64_t fold_mask(const tvec *v) {
    uint64_t f = 0;
    for (int d = 0; d < RD; d++)
        if (v->m[d >> 5] & (1u << (d & 31))) f |= 1ULL << (d >> 2);
    return f;
}
static void two_stage(void) {
    printf("\n  === coarse-to-fine: signatures in SRAM, flash only for survivors ===\n");
    SIG64 = malloc((size_t)R.n_index * 8);
    if (!SIG64) { printf("    alloc failed (%u KB needed)\n", (unsigned)(R.n_index*8/1024)); return; }
    for (uint32_t i = 0; i < R.n_index; i++) SIG64[i] = fold_mask(&TI[i]);
    printf("    signature table: %u KB in SRAM, heap left %u B\n",
           (unsigned)(R.n_index * 8 / 1024), (unsigned)esp_get_free_heap_size());

    const uint8_t *p = REFP; int agree = 0; int64_t tot = 0;
    for (uint32_t K = 64; K <= 512; K *= 4) {
        p = REFP; agree = 0; tot = 0;
        for (uint32_t r = 0; r < NREF; r++) {
            uint8_t len = *p++; char txt[128];
            memcpy(txt, p, len); txt[len] = 0; p += len;
            int32_t hs; memcpy(&hs, p, 4); p += 4;
            int8_t hc = (int8_t)*p++;
            int64_t t0 = esp_timer_get_time();
            tvec q; t_encode(&R, txt, &q); int aa = t_active(&q);
            uint64_t qf = fold_mask(&q);
            /* stage 1: rank by folded-mask overlap, entirely in SRAM */
            int thr = 0; static uint16_t hist[65];
            memset(hist, 0, sizeof hist);
            for (uint32_t i = 0; i < R.n_index; i++) hist[__builtin_popcountll(SIG64[i] & qf)]++;
            { uint32_t acc = 0; for (int b = 64; b >= 0; b--) { acc += hist[b]; if (acc >= K) { thr = b; break; } } }
            /* stage 2: full score only for survivors — the only flash reads */
            int best = -(1 << 28); uint32_t bi = 0, seen = 0;
            for (uint32_t i = 0; i < R.n_index; i++) {
                if (__builtin_popcountll(SIG64[i] & qf) < thr) continue;
                seen++;
                int s = t_score_pre(&q, &TI[i], aa, ACT[i]);
                if (s > best) { best = s; bi = i; }
            }
            tot += esp_timer_get_time() - t0;
            int cls = best <= R.threshold ? -1 : r_apply_polarity(&R, R.label[bi], txt);
            if (cls == hc && best == hs) agree++;
            (void)seen;
        }
        printf("    K>=%-4u  %6lld us/query  exact-match vs full scan: %d/%lu  (%.2fx vs 43498)\n",
               (unsigned)K, tot / NREF, agree, (unsigned long)NREF, 43498.0 / (double)(tot / NREF));
    }
    free(SIG64);
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
    /* CONFOUND: the three flashed indexes differed in CONTENT as well as size,
     * so "time is proportional to bytes" could be a content effect (branch
     * behaviour in the argmax update, say). Same blob, same vectors, only N
     * varies — this isolates size completely. */
    printf("  -- pure SIZE sweep: identical content, only N varies --\n");
    {
        uint32_t sz[] = { 1000, 2000, 4000, 6000, 8000, 10500 };
        for (int i = 0; i < 6; i++) {
            if (sz[i] > full) continue;
            char lb[64];
            snprintf(lb, sizeof lb, "N=%-6lu (%3lu KB)", (unsigned long)sz[i],
                     (unsigned long)(sz[i] * sizeof(tvec) / 1024));
            int64_t med = reps(lb, 1, sz[i]);
            printf("        -> %lld ns/vector\n", med * 1000 / sz[i]);
        }
    }
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
    dram_vs_flash();
    bulk_and_pipeline();
    two_stage();
    printf("\n===== %s =====\n",
           (agree_cls == (int)NREF && agree_score == (int)NREF) ? "PARITY EXACT" : "PARITY FAILED");
    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}
