#include "ternary.h"
#include <string.h>

/* Xtensa LX6 has no popcount instruction; GCC emits a ~15-op SWAR sequence.
 * t_dot runs 16 of them per vector and the on-device profile put that at 47%
 * of scan time - the largest single cost. A table turns each into loads.
 *
 * The table MUST live in DRAM, not IRAM: ESP32-classic IRAM only supports
 * 32-bit aligned access, so a byte load from it faults. Internal SRAM is
 * uncached and directly addressed, so lookups are uniform-latency - unlike
 * .rodata, which lands in flash and would be read through the cache MMU.
 *
 * TPOPCNT: 0 = builtin (baseline), 1 = 8-bit table, 2 = 16-bit table. */
#ifndef TPOPCNT
#define TPOPCNT 1
#endif
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define TDRAM DRAM_ATTR
#else
#define TDRAM
#endif

#if TPOPCNT == 1
#define P4(n)  n, n+1, n+1, n+2
#define P6(n)  P4(n), P4(n+1), P4(n+1), P4(n+2)
#define P8(n)  P6(n), P6(n+1), P6(n+1), P6(n+2)
static TDRAM const uint8_t PC8[256] = { P8(0), P8(1), P8(1), P8(2) };
static inline int pc(uint32_t x) {
    return PC8[x & 0xff] + PC8[(x >> 8) & 0xff]
         + PC8[(x >> 16) & 0xff] + PC8[x >> 24];
}
#elif TPOPCNT == 2
/* 64 KB, filled at init rather than linked in, so it costs .bss not image. */
static TDRAM uint8_t PC16[65536];
static inline int pc(uint32_t x) { return PC16[x & 0xffff] + PC16[x >> 16]; }
#else
static inline int pc(uint32_t x) { return __builtin_popcount(x); }
#endif

void t_popcnt_init(void) {
#if TPOPCNT == 2
    for (int i = 0; i < 65536; i++) PC16[i] = (uint8_t)__builtin_popcount(i);
#endif
}

void t_encode(const router_t *r, const char *text, tvec *out) {
    int16_t acc[RD]; int32_t total;
    r_counts(text, acc, &total);
    memset(out, 0, sizeof *out);
    for (int i = 0; i < RD; i++) {
        if (!acc[i]) continue;                      /* no evidence -> trit 0 */
        out->m[i >> 5] |= 1u << (i & 31);
        if ((int32_t)acc[i] * RSCALE >= r->centre[i] * total)
            out->s[i >> 5] |= 1u << (i & 31);
    }
}
/* dot = pc(both & ~diff) - pc(both & diff), rewritten as
 *       pc(both) - 2*pc(both & diff): same two popcounts, one fewer AND/NOT. */
int t_dot(const tvec *a, const tvec *b) {
    int agree = 0, disagree = 0;
    for (int i = 0; i < RWORDS; i++) {
        uint32_t both = a->m[i] & b->m[i];
        uint32_t diff = a->s[i] ^ b->s[i];
        agree    += pc(both);
        disagree += pc(both & diff);
    }
    return agree - 2 * disagree;
}
int t_active(const tvec *v) {
    int n = 0;
    for (int i = 0; i < RWORDS; i++) n += pc(v->m[i]);
    return n;
}
/* TSMOOTH: smoothing in the Dice denominator. Suspected of being a constant
 * implicitly fitted at d=256 that would handicap smaller d (where the active
 * counts halve, doubling its relative weight). Measured, and the suspicion was
 * WRONG in direction: d=128 prefers 16, not 4 — the optimum moves opposite to
 * dimension. Scaling it as RD/32 makes small d worse, so it stays constant.
 * Swept at d=256: 2/4/8/16 are tied within noise and only 32 degrades, so the
 * shipping config is not perched on a tuned peak. */
#ifndef TSMOOTH
#define TSMOOTH 8
#endif
/* Dice: 2*dot / (|a| + |b|). Symmetric and length-invariant, unlike dividing
 * by |b| alone — which made the score drift 23% with query length (149->184),
 * biasing strict-on-short / loose-on-long. IoT commands are short; the
 * negatives are long, so the bias ran exactly the wrong way. */
int t_score(const tvec *q, const tvec *b, int aa) {
    int d = t_dot(q, b);
    int ab = t_active(b);
    return (2 * d * 256) / (aa + ab + TSMOOTH);
}

int t_score_pre(const tvec *q, const tvec *b, int aa, int ab) {
    return (2 * t_dot(q, b) * 256) / (aa + ab + TSMOOTH);
}
