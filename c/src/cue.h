/* cue.h — hard word cues, DERIVED from the index, never hand-written.
 *
 * Motivation: the residual wrong-action errors cluster, and the largest cluster
 * is colour commands routed away from lightchange. The tempting fix is to read
 * the dev failures and hand-write a colour list. That is the archived failure
 * (a lexicon fitted to observed failures; the leakage-free version gained
 * nothing). So the word list is MINED FROM THE INDEX at build time: any word
 * whose lift for a class exceeds CUE_LIFT with at least CUE_MIN occurrences.
 *
 * Distinct from prior.c, which was a soft bias over all classes and measured
 * inert. This is a hard override for a small set of very high-lift words, and
 * it fires only inside a class family, so it can reassign an operation but
 * never invent a device.
 *
 * Integer throughout: lift is stored scaled by CUE_SCALE. */
#ifndef CUE_H
#define CUE_H
#include "router.h"
#define CUE_HASH  32768
#define CUE_SCALE 256
#ifndef CUE_LIFT
#define CUE_LIFT  40      /* minimum lift, in whole multiples */
#endif
#ifndef CUE_MIN
#define CUE_MIN   4       /* minimum occurrences in the index */
#endif
typedef struct {
    int16_t cls[CUE_HASH];    /* -1 = no cue */
    int32_t lift[CUE_HASH];   /* scaled by CUE_SCALE */
} cue_t;
void cue_build(cue_t *c, char **texts, const uint8_t *labels, int n, int n_class);
/* returns a class to override to, or -1. Only fires within cls's family. */
int  cue_apply(const cue_t *c, const router_t *r, const char *text, int cls);
#endif
