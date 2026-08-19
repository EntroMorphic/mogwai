/* prior.h — a prior derived from a channel the evidence cannot see.
 *
 * The evidence reader is char 3/4-gram hashing: order-insensitive, collision-
 * prone, and it destroys word identity. So the prior is built from WORDS —
 * information the hash provably cannot carry.
 *
 * The previous signature prior was inert (identical results at every
 * attenuation) because it was a majority-bit summary of the same hash vectors
 * retrieval scans. A prior that mirrors the evidence channel cannot disagree,
 * and a disagreement detector that cannot fire is architecture without function.
 *
 * Combination follows the-reflex: agreement biases, disagreement decays softly,
 * and strong disagreement zeroes the bias entirely. The prior never overrides
 * evidence; it only shapes sensitivity.  Integer throughout. */
#ifndef PRIOR_H
#define PRIOR_H
#include "router.h"
#define PR_HASH 32768
#define PR_SCALE 256

typedef struct {
    int32_t  n_class;
    int32_t  w_cls[PR_HASH][RMAXCLS];   /* word-hash -> per-class count */
    int32_t  w_tot[PR_HASH];            /* word-hash -> total count     */
    int32_t  cls_n[RMAXCLS];            /* utterances per class         */
    int32_t  n_total;                   /* index size, for lift          */
} prior_t;

void pr_build(prior_t *p, char **texts, uint8_t *labels, int n, int n_class);
/* returns best class and fills margin (top - second), both scaled by PR_SCALE */
int  pr_vote(const prior_t *p, const char *text, int *margin);
#endif
