/* router.h — zero-parameter, ZERO-FLOAT NL command router.
 *
 * Retrieval: char 3/4-gram hash -> per-dim integer threshold -> 1 bit/dim,
 *            nearest neighbour by popcount. 64 bytes per indexed utterance.
 * Polarity:  deterministic cue scan, overrides within a polarity pair.
 * Veto:      per-class binary signature; family disagreement abstains.
 *
 * No float anywhere. No learned parameters. int32 is the widest type used. */
#ifndef ROUTER_H
#define ROUTER_H
#include <stdint.h>
#include <stddef.h>

#ifndef RD
#define RD        256
#endif
#define RWORDS    (RD / 32)          /* 8 words per bit-plane; a tvec holds two
                                        (mask+sign) = 16 words = 64 bytes */
#define RMAXCLS   16
#define RNAMELEN  32
#define RSCALE    16384              /* fixed-point scale for the centre */
#define RMAGIC    0x52545231u        /* 'RTR1' */

/* Shipped operating point. This is what tune() returns, but that is not why it
   is here — it was moved to 126 on dev evidence and moved BACK by a held-out
   measurement, which is the more useful record.

   On dev, 126 looked clearly better: recall 85.9 -> 88.0, missed 14 -> 8, for
   1 -> 3 unbidden actuations. On the held-out set (test eval #3) the gain did
   not transfer:

     th=136   recall 84.1%   fa= 8  wa=15  missed=20
     th=126   recall 85.5%   fa=13  wa=16  missed=16

   +3 commands recognised, and +5 unbidden actuations to get them. The recall
   comparison is paired and one-directional — an item correct at 136 has
   score>136>126 and cannot get worse — so b=3, c=0, exact p=0.25. Not
   significant. Meanwhile the unbidden rate nearly doubled, 0.29% -> 0.47%.

   Paying a measurable safety cost for a recall gain indistinguishable from zero
   is the wrong trade for something that actuates hardware. Reverted per the
   falsifier pre-registered before the run.

   Read with doc/METHOD.md: recall cannot see false actuations; fa is the number
   that matters here. mkblob and compare --ship both read this constant. */
#define RSHIP_TH  136

typedef struct { uint32_t w[RWORDS]; } rvec;

typedef struct {
    uint32_t magic, dim, n_index, n_class;
    char     names[RMAXCLS][RNAMELEN];
    int32_t  centre[RD];             /* per-dim mean count * RSCALE, integer */
    int32_t  threshold;              /* accept if sim > threshold */
    rvec     sig[RMAXCLS];
    rvec    *index;
    uint8_t *label;
    void    *blob;                   /* owned allocation, or NULL if static */
} router_t;

void r_encode(const router_t *r, const char *text, rvec *out);
int  r_sim(const rvec *a, const rvec *b);
int  r_route(const router_t *r, const char *text, int *score_out);
int  r_polarity(const char *text);
int  r_apply_polarity(const router_t *r, int cls, const char *text);
int  r_family(const router_t *r, int cls);
int  r_norm(const char *in, char *out, int cap);
uint32_t r_fnv(const char *s, int n);
void r_counts(const char *text, int16_t *acc, int32_t *total);

int  r_load(router_t *r, const char *path);
void r_free(router_t *r);
#endif
