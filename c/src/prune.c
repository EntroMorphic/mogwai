#include "prune.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int prune_parse(const char *a, prune_opt *o) {
    if (!strcmp(a, "--prune-dup")) { o->dup = 1; return 1; }
    if (!strcmp(a, "--prune-cnn")) { o->cnn = 1; return 1; }
    if (!strncmp(a, "--prune-neg=", 12)) { o->neg_k = atoi(a + 12); return 1; }
    if (!strncmp(a, "--prune-negtop=", 15)) { o->neg_top = atoi(a + 15); return 1; }
    return 0;
}

int prune_index(char **U_t, char (*U_l)[RNAMELEN], router_t *R,
                tvec *TI, rvec *idx, int U_n, prune_opt o, int verbose) {
    if (!o.dup && !o.cnn && o.neg_k <= 1 && o.neg_top <= 0) return U_n;
    char *keep = malloc(U_n); memset(keep, 1, U_n);
    int nonec = -1;
    for (uint32_t c = 0; c < R->n_class; c++)
        if (!strcmp(R->names[c], "none")) nonec = (int)c;

    /* (a) exact-duplicate code with the same label. 256-dim hashing can map
       distinct strings onto identical codes; a copy cannot change any argmax
       except by tie-break order. Free to drop. (Measured: zero exist at d=256 —
       twin-ternary is injective on this corpus. Kept for smaller d.) */
    int ndup = 0;
    if (o.dup) {
        enum { HB = 1 << 15 };
        int *head = malloc(HB * sizeof(int)), *nxt = malloc(U_n * sizeof(int));
        for (int i = 0; i < HB; i++) head[i] = -1;
        for (int i = 0; i < U_n; i++) {
            const unsigned char *b = (const unsigned char *)&TI[i];
            uint32_t h = 2166136261u;
            for (size_t k = 0; k < sizeof(tvec); k++) { h ^= b[k]; h *= 16777619u; }
            uint32_t s = h & (HB - 1); int hit = 0;
            for (int j = head[s]; j >= 0; j = nxt[j])
                if (R->label[j] == R->label[i] &&
                    !memcmp(&TI[j], &TI[i], sizeof(tvec))) { hit = 1; break; }
            if (hit) { keep[i] = 0; ndup++; }
            else { nxt[i] = head[s]; head[s] = i; }
        }
        free(head); free(nxt);
    }
    /* (b) condensation: a negative earns its 64 bytes only if it is the nearest
       neighbour of something. Leave-one-out over the INDEX ONLY — the index is
       train, so this leaks neither dev nor test. */
    /* cov[j] = how many index entries have j as their nearest neighbour.
       cov[j] > 0 is exactly the old boolean `useful` flag, so --prune-cnn is
       bit-identical to before; --prune-negtop reads the same counts as a
       GRADED usefulness rank instead of thresholding them at zero. */
    int ncnn = 0;
    int *cov = NULL;
    if (o.cnn || o.neg_top > 0) {
        cov = calloc(U_n, sizeof(int));
        int *act = malloc(U_n * sizeof(int));
        for (int i = 0; i < U_n; i++) act[i] = t_active(&TI[i]);
        for (int i = 0; i < U_n; i++) {
            int best = -(1 << 28), bj = -1;
            for (int j = 0; j < U_n; j++) {
                if (j == i) continue;
                int s = t_score_pre(&TI[i], &TI[j], act[i], act[j]);
                if (s > best) { best = s; bj = j; }
            }
            if (bj >= 0) cov[bj]++;
        }
        free(act);
    }
    if (o.cnn) {
        for (int i = 0; i < U_n; i++)
            if (keep[i] && (int)R->label[i] == nonec && !cov[i])
                { keep[i] = 0; ncnn++; }
    }
    /* (b2) graded condensation: keep the o.neg_top negatives with the highest
       NN-coverage, dropping the rest. --prune-cnn is the special case "keep
       every negative with cov > 0" and cannot hit an arbitrary byte budget;
       this can, and it selects by the same criterion rather than at random
       (measured: condensation dominates random subsampling at equal bytes).
       Ties inside a coverage level break by index order, so it is
       deterministic and reproducible. */
    int ntop = 0;
    if (o.neg_top > 0) {
        int maxcov = 0;
        for (int i = 0; i < U_n; i++)
            if (keep[i] && (int)R->label[i] == nonec && cov[i] > maxcov) maxcov = cov[i];
        char *sel = calloc(U_n, 1);
        int budget = o.neg_top;
        for (int lvl = maxcov; lvl >= 0 && budget > 0; lvl--)
            for (int i = 0; i < U_n && budget > 0; i++)
                if (keep[i] && (int)R->label[i] == nonec && cov[i] == lvl)
                    { sel[i] = 1; budget--; }
        for (int i = 0; i < U_n; i++)
            if (keep[i] && (int)R->label[i] == nonec && !sel[i])
                { keep[i] = 0; ntop++; }
        free(sel);
    }
    free(cov);
    /* (c) negative subsample. Lossy and dominated by (b) at equal bytes. */
    int nneg = 0;
    if (o.neg_k > 1) {
        int seen = 0;
        for (int i = 0; i < U_n; i++)
            if (keep[i] && (int)R->label[i] == nonec && (seen++ % o.neg_k))
                { keep[i] = 0; nneg++; }
    }
    int kept = 0, iot_k = 0, neg_k = 0;
    for (int i = 0; i < U_n; i++) {
        if (!keep[i]) continue;
        if (U_t) U_t[kept] = U_t[i];
        if (U_l) memcpy(U_l[kept], U_l[i], RNAMELEN);
        if (idx) idx[kept] = idx[i];
        TI[kept] = TI[i]; R->label[kept] = R->label[i];
        if ((int)R->label[i] == nonec) neg_k++; else iot_k++;
        kept++;
    }
    if (verbose)
        fprintf(stderr, "  [prune] %d -> %d  (dup %d, cnn %d, negtop %d, "
                        "neg-subsample %d)  iot %d / none %d  | %.0f KB\n",
                U_n, kept, ndup, ncnn, ntop, nneg, iot_k, neg_k,
                kept * sizeof(tvec) / 1024.0);
    R->n_index = kept;
    free(keep);
    return kept;
}
