#include "cascade.h"
#include <string.h>
#include <stdlib.h>

int c_mask_overlap(const tvec *a, const tvec *b) {
    int ov = 0;
    for (int i = 0; i < RWORDS; i++) ov += __builtin_popcount(a->m[i] & b->m[i]);
    return ov;
}
/* split normalised text into word hashes */
static int words(const char *t, uint32_t *out, int cap) {
    char b[512]; int n = r_norm(t, b, sizeof b), k = 0;
    int s = -1;
    for (int i = 0; i <= n; i++) {
        int sp = (b[i] == ' ' || b[i] == 0);
        if (!sp && s < 0) s = i;
        else if (sp && s >= 0) { if (k < cap) out[k++] = r_fnv(b + s, i - s); s = -1; }
    }
    return k;
}
/* word-set Dice in integers: 2|A∩B| / (|A|+|B|), scaled 256.
 * No hashing collisions of the 512-dim index, no dimensional bottleneck. */
int c_text_sim(const char *qa, const char *qb) {
    uint32_t A[64], B[64];
    int na = words(qa, A, 64), nb = words(qb, B, 64);
    if (!na || !nb) return 0;
    int inter = 0;
    for (int i = 0; i < na; i++) {
        int dup = 0;
        for (int j = 0; j < i; j++) if (A[j] == A[i]) { dup = 1; break; }
        if (dup) continue;
        for (int j = 0; j < nb; j++) if (A[i] == B[j]) { inter++; break; }
    }
    return (2 * inter * 256) / (na + nb);
}
