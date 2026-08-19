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
#define RWORDS    (RD / 32)          /* 16 words = 64 bytes per vector */
#define RMAXCLS   16
#define RNAMELEN  32
#define RSCALE    16384              /* fixed-point scale for the centre */
#define RMAGIC    0x52545231u        /* 'RTR1' */

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
