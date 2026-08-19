#include "ternary.h"
#include <string.h>

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
int t_dot(const tvec *a, const tvec *b) {
    int agree = 0, disagree = 0;
    for (int i = 0; i < RWORDS; i++) {
        uint32_t both = a->m[i] & b->m[i];
        uint32_t diff = a->s[i] ^ b->s[i];
        agree    += __builtin_popcount(both & ~diff);
        disagree += __builtin_popcount(both &  diff);
    }
    return agree - disagree;
}
int t_active(const tvec *v) {
    int n = 0;
    for (int i = 0; i < RWORDS; i++) n += __builtin_popcount(v->m[i]);
    return n;
}
/* integer normalisation: longer index entries must not win on support alone */
int t_score(const tvec *q, const tvec *b) {
    int d = t_dot(q, b);
    int ab = t_active(b);
    return (d * 256) / (ab + 8);
}
