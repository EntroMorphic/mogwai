/* twin-ternary: two bit-planes per vector.
 *   m[] mask  — 1 if this dim carries evidence
 *   s[] sign  — 1 if above centre (only meaningful where m=1)
 * dot = popcount(ma&mb&~(sa^sb)) - popcount(ma&mb&(sa^sb))
 * Binary forces empty dims to -1, so sparse vectors agree on hundreds of dims
 * that carry no information. Ternary lets "no evidence" be its own state. */
#ifndef TERNARY_H
#define TERNARY_H
#include "router.h"
typedef struct { uint32_t m[RWORDS], s[RWORDS]; } tvec;
void t_encode(const router_t *r, const char *text, tvec *out);
int  t_dot(const tvec *a, const tvec *b);       /* raw agreement, [-RD,+RD] */
int  t_score(const tvec *q, const tvec *b, int aa); /* Dice: symmetric, length-invariant */
/* identical arithmetic, but takes the index vector's active count instead of
   recomputing it. t_active(b) is fixed at build time and was costing 569 of
   2335 cycles per vector on ESP32 (24%) - 8 redundant popcounts per compare. */
int  t_score_pre(const tvec *q, const tvec *b, int aa, int ab);
int  t_active(const tvec *v);
void t_popcnt_init(void);   /* no-op unless the 16-bit table is compiled in */

/* ---- v2: mask + exception stream ----------------------------------------
 * The sign plane stored as the 1,539 exceptions it actually is. See router.h
 * for the algebra; the short version is
 *
 *     disagree = |Eq & bm| + |Eb & qm| - 2*|Eq & Eb|
 *
 * which is exact, so t_dot_ex == t_dot for every input. Eight popcounts
 * instead of sixteen, plus bit tests on sets empty for 89.3% of vectors.
 * Positions are uint8 and ascending; RD <= 256 is load-bearing. */
int t_exceptions(const tvec *v, uint8_t *out);   /* -> count, out[] ascending */
int t_dot_ex(const uint32_t *qm, const uint8_t *Eq, int nq,
             const uint32_t *bm, const uint8_t *Eb, int nb);
int t_score_ex(const uint32_t *qm, const uint8_t *Eq, int nq,
               const uint32_t *bm, const uint8_t *Eb, int nb, int aa, int ab);
int t_active_m(const uint32_t *m);               /* popcount of a bare mask */
#endif
