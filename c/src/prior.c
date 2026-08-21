#include "prior.h"
#include <string.h>
#include <stdlib.h>

int words_of(const char *t, uint32_t *out, int cap) {
    char b[512]; int n = r_norm(t, b, sizeof b), k = 0, s = -1;
    for (int i = 0; i <= n; i++) {
        int sp = (b[i] == ' ' || b[i] == 0);
        if (!sp && s < 0) s = i;
        else if (sp && s >= 0) { if (k < cap) out[k++] = r_fnv(b + s, i - s) % PR_HASH; s = -1; }
    }
    return k;
}
void pr_build(prior_t *p, char **texts, uint8_t *labels, int n, int n_class) {
    memset(p, 0, sizeof *p);
    p->n_class = n_class; p->n_total = n;
    uint32_t w[64];
    for (int i = 0; i < n; i++) {
        int k = words_of(texts[i], w, 64);
        p->cls_n[labels[i]]++;
        for (int j = 0; j < k; j++) {
            int dup = 0;
            for (int m = 0; m < j; m++) if (w[m] == w[j]) { dup = 1; break; }
            if (dup) continue;
            p->w_cls[w[j]][labels[i]]++;
            p->w_tot[w[j]]++;
        }
    }
}
/* LIFT, not P(c|word).
 *
 * Raw P(c|word) is a popularity contest: "none" holds ~88% of the index, so it
 * carries more mass for almost every word and wins by default (measured: the
 * prior voted none 88% of the time and was correct 11.4%). Lift divides that
 * out — a word is evidence FOR c only if it appears in c more than c's base
 * rate would predict:
 *
 *      lift = P(word|c) / P(word)  =  (cnt[w][c]/n_c) / (tot[w]/N)
 *
 * All integer: (cnt * N * SCALE) / (n_c * tot). lift > SCALE means "favours c".
 * Contributions are clamped so one rare word cannot dominate the sum. */
int pr_vote(const prior_t *p, const char *text, int *margin) {
    uint32_t w[64];
    int k = words_of(text, w, 64);
    int32_t sc[RMAXCLS]; memset(sc, 0, sizeof sc);
    int informative = 0;
    for (int j = 0; j < k; j++) {
        int dup = 0;
        for (int m = 0; m < j; m++) if (w[m] == w[j]) { dup = 1; break; }
        if (dup) continue;
        int64_t tot = p->w_tot[w[j]];
        if (tot < 2) continue;
        informative++;
        for (int c = 0; c < p->n_class; c++) {
            int64_t cnt = p->w_cls[w[j]][c];
            if (!cnt || !p->cls_n[c]) continue;
            int64_t lift = (cnt * p->n_total * PR_SCALE) / ((int64_t)p->cls_n[c] * tot);
            int64_t d = lift - PR_SCALE;
            if (d >  4 * PR_SCALE) d =  4 * PR_SCALE;   /* clamp: no single word dominates */
            if (d < -PR_SCALE)     d = -PR_SCALE;
            sc[c] += (int32_t)d;
        }
    }
    if (!informative) { if (margin) *margin = 0; return -1; }   /* abstain */
    int b1 = -(1<<30), b2 = -(1<<30), bi = -1;
    for (int c = 0; c < p->n_class; c++) {
        if (sc[c] > b1) { b2 = b1; b1 = sc[c]; bi = c; }
        else if (sc[c] > b2) b2 = sc[c];
    }
    /* margin RELATIVE to the winner, in our own units — not a trit count
       imported from a different representation. */
    if (margin) *margin = (b1 > 0) ? (int)(((int64_t)(b1 - b2) * 100) / b1) : 0;
    return bi;
}
