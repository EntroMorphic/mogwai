#include "gate.h"
#include <string.h>
#include <stdio.h>

/* words_of lives in prior.c; same hashing, same normalisation — the gate must
   bucket words identically or it is a different model, not a smaller one. */
int words_of(const char *text, uint32_t *w, int cap);

void gate_build(gate_t *g, const prior_t *p) {
    memset(g, 0, sizeof *g);
    g->n_class = p->n_class;
    uint32_t n = 0;
    for (int h = 0; h < PR_HASH; h++) {
        g->off[h] = (uint16_t)n;
        int64_t tot = p->w_tot[h];
        if (tot >= 2) {
            for (int c = 0; c < p->n_class; c++) {
                int64_t cnt = p->w_cls[h][c];
                if (!cnt || !p->cls_n[c]) continue;
                int64_t lift = (cnt * p->n_total * PR_SCALE) / ((int64_t)p->cls_n[c] * tot);
                int64_t d = lift - PR_SCALE;
                if (d >  4 * PR_SCALE) d =  4 * PR_SCALE;
                if (d < -PR_SCALE)     d = -PR_SCALE;
                if (!d) continue;
                if (n >= GATE_MAXPAIR) { fprintf(stderr, "gate: pair overflow\n"); return; }
                g->cls[n] = (uint8_t)c; g->del[n] = (int16_t)d; n++;
            }
        }
    }
    g->off[PR_HASH] = (uint16_t)n;
    g->npair = n;
    /* gate_vote treats "no pairs" as "w_tot < 2". That holds only if every live
       bucket produced at least one nonzero delta. Measured true here, but it is
       data-dependent, so check it rather than assume it. */
    for (int h = 0; h < PR_HASH; h++) {
        int live = p->w_tot[h] >= 2, pairs = g->off[h + 1] > g->off[h];
        if (live != pairs) {
            fprintf(stderr, "gate: bucket %d live=%d pairs=%d - liveness test invalid\n",
                    h, live, pairs);
            return;
        }
    }
}
size_t gate_bytes(const gate_t *g) {
    return sizeof g->off + g->npair * (sizeof(uint8_t) + sizeof(int16_t));
}
int gate_vote(const gate_t *g, const char *text, int *margin) {
    uint32_t w[64];
    int k = words_of(text, w, 64);
    int32_t sc[RMAXCLS]; memset(sc, 0, sizeof sc);
    int informative = 0;
    for (int j = 0; j < k; j++) {
        int dup = 0;
        for (int m = 0; m < j; m++) if (w[m] == w[j]) { dup = 1; break; }
        if (dup) continue;
        uint32_t a = g->off[w[j]], b = g->off[w[j] + 1];
        if (a == b) continue;                   /* dead bucket == w_tot < 2 */
        informative++;
        for (uint32_t t = a; t < b; t++) sc[g->cls[t]] += g->del[t];
    }
    if (!informative) { if (margin) *margin = 0; return -1; }
    int b1 = -(1 << 30), b2 = -(1 << 30), bi = -1;
    for (int c = 0; c < g->n_class; c++) {
        if (sc[c] > b1) { b2 = b1; b1 = sc[c]; bi = c; }
        else if (sc[c] > b2) b2 = sc[c];
    }
    if (margin) *margin = (b1 > 0) ? (int)(((int64_t)(b1 - b2) * 100) / b1) : 0;
    return bi;
}
