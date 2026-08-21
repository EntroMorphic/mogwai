/* gate.h — the word prior, compacted for the device.
 *
 * `prior_t` is 2.13 MB (32768 x 16 int32 counts, plus totals) — it stores what
 * is needed to BUILD the prior, not what is needed to USE it. At inference
 * `pr_vote` only ever needs, per word bucket, the clamped per-class lift delta.
 *
 * (An earlier claim that argmax+margin per bucket would do was WRONG: pr_vote
 * sums deltas across the words of an utterance and takes the margin of the
 * AGGREGATE, so per-bucket argmax cannot reconstruct it.)
 *
 * Measured structure: only 2512 of 32768 buckets are live, carrying 3487
 * nonzero deltas — 1.39 per live bucket, and 82%% of live buckets hold exactly
 * one. So the table is sparse, and a CSR-style layout collapses it:
 *
 *     off[PR_HASH+1]  uint16   64 KB   bucket -> pair range
 *     cls[]           uint8     3.4 KB
 *     del[]           int16     6.8 KB
 *                             ~74 KB   vs 2.13 MB  = 29x
 *
 * gate_vote() must return bit-identical results to pr_vote(). That is asserted,
 * not assumed — see --gatecheck. */
#ifndef GATE_H
#define GATE_H
#include "prior.h"
#define GATE_MAXPAIR 16384
typedef struct {
    uint16_t off[PR_HASH + 1];
    uint8_t  cls[GATE_MAXPAIR];
    int16_t  del[GATE_MAXPAIR];
    uint32_t npair;
    int32_t  n_class;
} gate_t;
void gate_build(gate_t *g, const prior_t *p);
int  gate_vote(const gate_t *g, const char *text, int *margin);
size_t gate_bytes(const gate_t *g);
#endif
