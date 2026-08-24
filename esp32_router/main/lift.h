/* lift.h — put as much of the index into SRAM as is safe, in chunks.
 *
 * ONE implementation, used by product.c (which ships it) and wifiprobe.c (which
 * measures whether it survives a TLS handshake). Same reason prune.c is shared
 * between the harness and the exporter: if the thing measured and the thing
 * shipped allocate differently, the measurement is of nothing.
 *
 * v2: the lifted record is the 32-byte active MASK, not the 64-byte twin-ternary
 * vector. The sign plane is now a sparse exception stream (see router.h), which
 * is 1,539 bytes for the shipped index and rides along whole.
 */
#ifndef LIFT_H
#define LIFT_H
#include <stdint.h>
#include <stddef.h>
#include "ternary.h"

#define LIFT_CHUNK_VECS 256                  /* 8 KB per chunk at 32 B/vector */
#define LIFT_MAXCHUNK   512                  /* 131,072 vectors */

/* How much heap must remain after the lift.
 *
 * MEASURED, not guessed: a single TLS handshake dips 46,716 B below steady
 * state (wifiprobe, mbedTLS via esp_http_client). Steady-state free barely
 * moves across the same fetch — about 1.1 KB — so sampling free heap before and
 * after would have under-reported the requirement by a factor of 45.
 *
 *   LIFT_RESERVE_BARE  no network stack. What the shipped product uses.
 *   LIFT_RESERVE_TLS   anything that performs a TLS handshake, ever.
 *
 * Sizing the reserve from the steady state rather than the low-water mark is
 * the specific way this fails: the device boots, runs, and dies on its first
 * HTTPS request in the field. */
#define LIFT_RESERVE_BARE 40960
#define LIFT_RESERVE_TLS  61440              /* 46,716 measured + 31% margin */

typedef struct {
    const uint32_t *ch[LIFT_MAXCHUNK];       /* chunk -> SRAM copy, or into flash */
    const uint16_t *act;                     /* uint16_t: NEVER eligible for IRAM */
    const uint16_t *eoff;                    /* n_index + 1 */
    const uint8_t  *epos;                    /* uint8: NEVER eligible for IRAM */
    uint32_t n_index, nch, nex;
    uint32_t vec_dram, vec_iram;             /* lifted, by pool; vec_iram is 0
                                                on every shipped v2 config -- see
                                                the note in lift.c */
    uint32_t bad;                            /* addressing mismatches; must be 0 */
    size_t   need, largest;                  /* for reporting */
} lift_t;

/* Returns 0 on success, -1 if n_index exceeds the chunk table. Always leaves
 * L usable: unlifted chunks point into flash and score identically. */
int lift_run(lift_t *L, const uint32_t *mask, const uint16_t *act,
             const uint16_t *eoff, const uint8_t *epos, uint32_t nex,
             uint32_t n_index, size_t reserve);
#endif
