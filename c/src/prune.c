#include "prune.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int prune_parse(const char *a, prune_opt *o) {
    if (!strcmp(a, "--prune-dup")) { o->dup = 1; return 1; }
    if (!strcmp(a, "--prune-cnn")) { o->cnn = 1; return 1; }
    if (!strncmp(a, "--prune-neg=", 12)) { o->neg_k = atoi(a + 12); return 1; }
    if (!strncmp(a, "--prune-negtop=", 15)) { o->neg_top = atoi(a + 15); return 1; }
    if (!strncmp(a, "--prune-negbound=", 17)) { o->neg_bound = atoi(a + 17); return 1; }
    if (!strncmp(a, "--prune-neghalo=", 16))  { o->neg_halo  = atoi(a + 16); return 1; }
    return 0;
}

int prune_index(char **U_t, char (*U_l)[RNAMELEN], router_t *R,
                tvec *TI, rvec *idx, int U_n, prune_opt o, int verbose) {
    if (!o.dup && !o.cnn && o.neg_k <= 1 && o.neg_top <= 0 && o.neg_bound <= 0 && o.neg_halo <= 0) return U_n;
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
    /* (b3) BOUNDARY-WITNESS selection.
     *
     * negtop ranks a negative by how many INDEX ENTRIES have it as their
     * nearest neighbour - and 89% of the index is negatives, so that count is
     * dominated by negative-to-negative structure. It rewards a negative for
     * representing negative space. What rejection actually needs is negatives
     * sitting on the boundary AROUND POSITIVE space: the ones a command-like
     * non-command would land on.
     *
     * Measured motivation: the shipped 2685-negative index has fa=6 on dev, the
     * unpruned 9345-negative index has fa=1. The rejection information existed
     * and negtop discarded it.
     *
     * So count differently: for each POSITIVE exemplar find its nearest
     * negatives and credit those. Same budget, same leave-one-out discipline,
     * index-only - no dev, no test. */
    int nbound = 0;
    if (o.neg_bound > 0) {
        int *bcov = calloc(U_n, sizeof(int));
        int *bact = malloc(U_n * sizeof(int));
        for (int i = 0; i < U_n; i++) bact[i] = t_active(&TI[i]);
        const int KW = 4;                       /* boundary witnesses per positive */
        for (int i = 0; i < U_n; i++) {
            if (!keep[i] || (int)R->label[i] == nonec) continue;   /* positives only */
            int bs[4], bj[4];
            for (int k = 0; k < KW; k++) { bs[k] = -(1 << 28); bj[k] = -1; }
            for (int j = 0; j < U_n; j++) {
                if (j == i || !keep[j] || (int)R->label[j] != nonec) continue;
                int s = t_score_pre(&TI[i], &TI[j], bact[i], bact[j]);
                if (s <= bs[KW-1]) continue;
                int k = KW - 1;
                while (k > 0 && bs[k-1] < s) { bs[k] = bs[k-1]; bj[k] = bj[k-1]; k--; }
                bs[k] = s; bj[k] = j;
            }
            for (int k = 0; k < KW; k++) if (bj[k] >= 0) bcov[bj[k]]++;
        }
        free(bact);
        int maxb = 0;
        for (int i = 0; i < U_n; i++)
            if (keep[i] && (int)R->label[i] == nonec && bcov[i] > maxb) maxb = bcov[i];
        char *bsel = calloc(U_n, 1);
        int bbud = o.neg_bound;
        for (int lvl = maxb; lvl >= 0 && bbud > 0; lvl--)
            for (int i = 0; i < U_n && bbud > 0; i++)
                if (keep[i] && (int)R->label[i] == nonec && bcov[i] == lvl)
                    { bsel[i] = 1; bbud--; }
        for (int i = 0; i < U_n; i++)
            if (keep[i] && (int)R->label[i] == nonec && !bsel[i])
                { keep[i] = 0; nbound++; }
        free(bsel); free(bcov);
    }
    /* (b4) PER-CLASS rejection halo.
     *
     * negbound takes a global top-N by boundary-witness count, which lets one
     * class with a dense boundary starve another. A class whose exemplars sit
     * in a crowded region generates many witnesses; a sparse class generates
     * few, and its guards get outbid — even though it needs them just as much.
     *
     * So give every actuation class its own halo. Split the budget equally
     * across positive classes, let each select the negatives that most often
     * guard ITS exemplars, union the selections (a negative guarding two
     * classes is chosen once), then backfill any remainder by the global count.
     *
     * Equal split rather than proportional: the point is that no class goes
     * unguarded, and proportional allocation reintroduces exactly the starvation
     * it is meant to prevent. No parameter is tuned - the split is 1/n_classes.
     */
    int nhalo = 0;
    if (o.neg_halo > 0) {
        const int KH = 4;
        int *hact = malloc(U_n * sizeof(int));
        for (int i = 0; i < U_n; i++) hact[i] = t_active(&TI[i]);
        int *wit = malloc((size_t)U_n * KH * sizeof(int));
        for (size_t z = 0; z < (size_t)U_n * KH; z++) wit[z] = -1;

        /* one pass: every positive records its KH nearest negatives */
        for (int i = 0; i < U_n; i++) {
            if (!keep[i] || (int)R->label[i] == nonec) continue;
            int bs[4], bj[4];
            for (int k = 0; k < KH; k++) { bs[k] = -(1 << 28); bj[k] = -1; }
            for (int j = 0; j < U_n; j++) {
                if (j == i || !keep[j] || (int)R->label[j] != nonec) continue;
                int s = t_score_pre(&TI[i], &TI[j], hact[i], hact[j]);
                if (s <= bs[KH-1]) continue;
                int k = KH - 1;
                while (k > 0 && bs[k-1] < s) { bs[k] = bs[k-1]; bj[k] = bj[k-1]; k--; }
                bs[k] = s; bj[k] = j;
            }
            for (int k = 0; k < KH; k++) wit[(size_t)i * KH + k] = bj[k];
        }
        free(hact);

        int ncls = 0;
        for (uint32_t c = 0; c < R->n_class; c++) if ((int)c != nonec) ncls++;
        int per = ncls ? o.neg_halo / ncls : o.neg_halo;
        char *hsel = calloc(U_n, 1);
        int *cnt = calloc(U_n, sizeof(int));
        int chosen = 0;

        for (uint32_t c = 0; c < R->n_class && chosen < o.neg_halo; c++) {
            if ((int)c == nonec) continue;
            memset(cnt, 0, (size_t)U_n * sizeof(int));
            int mx = 0;
            for (int i = 0; i < U_n; i++) {
                if (!keep[i] || R->label[i] != c) continue;
                for (int k = 0; k < KH; k++) {
                    int j = wit[(size_t)i * KH + k];
                    if (j >= 0) { cnt[j]++; if (cnt[j] > mx) mx = cnt[j]; }
                }
            }
            int quota = per;
            for (int lvl = mx; lvl >= 1 && quota > 0; lvl--)
                for (int i = 0; i < U_n && quota > 0; i++)
                    if (keep[i] && (int)R->label[i] == nonec && cnt[i] == lvl && !hsel[i])
                        { hsel[i] = 1; quota--; chosen++; }
        }

        /* backfill: global boundary-witness count over every positive */
        if (chosen < o.neg_halo) {
            memset(cnt, 0, (size_t)U_n * sizeof(int));
            int mx = 0;
            for (int i = 0; i < U_n; i++) {
                if (!keep[i] || (int)R->label[i] == nonec) continue;
                for (int k = 0; k < KH; k++) {
                    int j = wit[(size_t)i * KH + k];
                    if (j >= 0) { cnt[j]++; if (cnt[j] > mx) mx = cnt[j]; }
                }
            }
            int rem = o.neg_halo - chosen;
            for (int lvl = mx; lvl >= 0 && rem > 0; lvl--)
                for (int i = 0; i < U_n && rem > 0; i++)
                    if (keep[i] && (int)R->label[i] == nonec && cnt[i] == lvl && !hsel[i])
                        { hsel[i] = 1; rem--; chosen++; }
        }
        for (int i = 0; i < U_n; i++)
            if (keep[i] && (int)R->label[i] == nonec && !hsel[i])
                { keep[i] = 0; nhalo++; }
        free(hsel); free(cnt); free(wit);
    }
    /* (c) negative subsample. Lossy and dominated by (b) at equal bytes. */
    int nneg = 0;
    if (o.neg_k > 1) {
        int seen = 0;
        for (int i = 0; i < U_n; i++)
            if (keep[i] && (int)R->label[i] == nonec && (seen++ % o.neg_k))
                { keep[i] = 0; nneg++; }
    }
    /* Guard coverage: for each actuation class, what fraction of its positives
       still have at least one of their nearest negatives in the index? That is
       the property the halo idea was meant to protect, so measure it rather
       than assume the selection criterion delivered it. */
    if (verbose && (o.neg_top > 0 || o.neg_bound > 0 || o.neg_halo > 0)) {
        const int KG = 4;
        int *gact = malloc(U_n * sizeof(int));
        for (int i = 0; i < U_n; i++) gact[i] = t_active(&TI[i]);
        int gtot[RMAXCLS] = {0}, ggrd[RMAXCLS] = {0};
        for (int i = 0; i < U_n; i++) {
            if ((int)R->label[i] == nonec) continue;
            /* witnesses computed over the FULL negative pool, guarded counted
               over survivors: "did this positive keep any of its guards" */
            int bs[4], bj[4];
            for (int k = 0; k < KG; k++) { bs[k] = -(1 << 28); bj[k] = -1; }
            for (int j = 0; j < U_n; j++) {
                if (j == i || (int)R->label[j] != nonec) continue;
                int s = t_score_pre(&TI[i], &TI[j], gact[i], gact[j]);
                if (s <= bs[KG-1]) continue;
                int k = KG - 1;
                while (k > 0 && bs[k-1] < s) { bs[k] = bs[k-1]; bj[k] = bj[k-1]; k--; }
                bs[k] = s; bj[k] = j;
            }
            gtot[R->label[i]]++;
            for (int k = 0; k < KG; k++)
                if (bj[k] >= 0 && keep[bj[k]]) { ggrd[R->label[i]]++; break; }
        }
        free(gact);
        fprintf(stderr, "  [guards] positives retaining >=1 of their %d nearest negatives:\n", KG);
        for (uint32_t c = 0; c < R->n_class; c++) {
            if ((int)c == nonec || !gtot[c]) continue;
            fprintf(stderr, "    %-24s %4d/%-4d  %3d%%\n",
                    R->names[c], ggrd[c], gtot[c], 100 * ggrd[c] / gtot[c]);
        }
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
        fprintf(stderr, "  [prune] %d -> %d  (dup %d, cnn %d, negtop %d, negbound %d, neghalo %d, "
                        "neg-subsample %d)  iot %d / none %d  | %.0f KB\n",
                U_n, kept, ndup, ncnn, ntop, nbound, nhalo, nneg, iot_k, neg_k,
                kept * sizeof(tvec) / 1024.0);
    R->n_index = kept;
    free(keep);
    return kept;
}
