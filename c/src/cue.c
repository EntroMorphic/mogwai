#include "cue.h"
#include <stdlib.h>
#include <string.h>

static unsigned wh(const char *w, int n) {
    unsigned h = 2166136261u;
    for (int i = 0; i < n; i++) { h ^= (unsigned char)w[i]; h *= 16777619u; }
    return h & (CUE_HASH - 1);
}
/* walk normalised text word by word; cb gets (hash) */
#define EACH_WORD(text, body) do {                                       \
    char _b[512]; r_norm((text), _b, sizeof _b);                         \
    char *_p = _b; int _n = 0; char _t[32];                              \
    for (;; _p++) {                                                      \
        if (*_p && *_p != ' ') { if (_n < 31) _t[_n++] = *_p; continue; }\
        if (_n) { unsigned wid = wh(_t, _n); (void)wid; body; _n = 0; }  \
        if (!*_p) break;                                                 \
    } } while (0)

void cue_build(cue_t *c, char **texts, const uint8_t *labels, int n, int n_class) {
    static int32_t wc[CUE_HASH][RMAXCLS], wt[CUE_HASH];
    static int32_t cn[RMAXCLS];
    memset(wc, 0, sizeof wc); memset(wt, 0, sizeof wt); memset(cn, 0, sizeof cn);
    for (int i = 0; i < n; i++) {
        int cl = labels[i]; cn[cl]++;
        EACH_WORD(texts[i], { wc[wid][cl]++; wt[wid]++; });
    }
    for (int h = 0; h < CUE_HASH; h++) { c->cls[h] = -1; c->lift[h] = 0; }
    for (int h = 0; h < CUE_HASH; h++) {
        if (wt[h] < CUE_MIN) continue;
        for (int cl = 0; cl < n_class; cl++) {
            if (!wc[h][cl] || !cn[cl]) continue;
            /* lift = (wc/wt) / (cn/n) = wc*n / (wt*cn), scaled */
            int64_t lift = ((int64_t)wc[h][cl] * n * CUE_SCALE) / ((int64_t)wt[h] * cn[cl]);
            if (lift >= (int64_t)CUE_LIFT * CUE_SCALE && lift > c->lift[h]) {
                c->lift[h] = (int32_t)lift; c->cls[h] = (int16_t)cl;
            }
        }
    }
}
int cue_apply(const cue_t *c, const router_t *r, const char *text, int cls) {
    if (cls < 0) return cls;
    int best = -1; int32_t bl = 0;
    EACH_WORD(text, {
        if (c->cls[wid] >= 0 && c->lift[wid] > bl) { bl = c->lift[wid]; best = c->cls[wid]; }
    });
    if (best < 0 || best == cls) return cls;
    /* fire only within the same device family: reassign an operation,
       never invent a device the utterance gives no evidence for */
    if (r_family(r, best) != r_family(r, cls)) return cls;
    return best;
}
