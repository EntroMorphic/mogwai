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
#define RMAGIC    0x52545231u        /* 'RTR1' — v1: m+s bit-planes, 64 B/vector */

/* v2 stores the sign plane as what it actually is.
 *
 * Measured over the shipped index: 78.51% of dims are 0, 21.34% are +1, and
 * 0.16% are -1 — 1,539 of 983,040. The sign plane is not 122,880 arbitrary
 * bits, it is 1,539 exceptions to a rule that holds 99.84% of the time, and
 * 83% of them are in `none` (a length signal: --condcentre showed removing
 * that asymmetry costs twenty points, so the plane is load-bearing and must
 * be preserved exactly, not dropped).
 *
 * So: keep the 32-byte active mask, make +1 implicit, and store a stream of
 * uint8 dimension positions where the sign is BELOW centre. With s = m & ~E,
 * inside `both` we have q.s = ~Eq and b.s = ~Eb, so diff there is Eq ^ Eb:
 *
 *     disagree = |Eq & bm| + |Eb & qm| - 2*|Eq & Eb|
 *
 * An identity for every input, so routing is bit-identical to v1 — which is
 * what PARITY EXACT proves on the device, since the reference scores embedded
 * in the blob are computed host-side from the v1 bit-planes.
 *
 * A new magic rather than a version field: a v1 blob must be REJECTED, not
 * misparsed. The layouts differ from the vector section onward. */
#define RMAGIC2   0x52545232u        /* 'RTR2' — v2: mask + exception stream */
#define RMASKB    (RD / 8)           /* 32 B: the active mask, fixed size */
#define REXMAX    65535u             /* the uint16 offset table bounds the
                                        exception stream; mkblob aborts above
                                        this rather than silently wrapping */

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

/* The shipped index keeps the RSHIP_NEGTOP negatives with the highest
   NN-coverage and drops the rest: 10500 -> 3840 vectors, 656 -> 240 KB.

   3840 is not an accuracy number, it is a memory number. The ESP32 lifts the
   index into SRAM in 8 KB chunks and 30 chunks is the most that fits under the
   40 KB reserve, so 30*128 = 3840 vectors is the largest index that is FULLY
   resident - flash untouched on every query, 34.3 -> 6.6 ms.

   The cost is false actuations and nothing else. Dev is identical to the
   unpruned index on every other axis - 85.9% +-2.5, wa 13, missed 14 - because
   at th=136 a negative is never an IoT item's nearest neighbour. fa goes 1 -> 6
   of 1335 non-commands. That is the whole trade, and it is the one property
   doc/METHOD.md says to weigh most heavily, so it is stated here rather than
   buried in a table.

   mkblob defaults to this and compare --ship reproduces it. */
#define RSHIP_NEGTOP 2685

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


/* v2 index, as pointers into the mapped blob. Nothing is copied: on the device
 * this points straight at flash until lift.c moves the masks into SRAM. */
typedef struct {
    const uint32_t *mask;        /* n_index * RWORDS, 4-aligned by layout */
    const uint16_t *eoff;        /* n_index + 1, ascending */
    const uint16_t *act;         /* precomputed t_active per entry */
    const uint8_t  *label;
    const uint8_t  *epos;        /* nex sign-exception positions */
    const uint8_t  *refp;
    uint32_t        nref, nex;
} rindex2;

/* Parse and VALIDATE a v2 blob. Returns 0, or:
 *   -1 bad magic or dim      -2 truncated / extent overflow
 *   -3 offsets not ascending -4 exception position out of range
 *   -5 an exception slice is not strictly ascending
 *   -6 the reference records overrun the blob
 * The extent check matters because magic and dim survive a truncated blob
 * while n_index does not describe what is really there. */
int r_parse2(router_t *r, rindex2 *ix, const uint8_t *base, size_t have);

int  r_load(router_t *r, const char *path);
void r_free(router_t *r);
#endif
