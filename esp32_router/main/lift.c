#include <stdlib.h>
#include <string.h>
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "lift.h"

/* IRAM is 32-bit-access-only. The lifted record is uint32_t mask[RWORDS], so a
 * whole-word scan is legal there — but memcpy/memcmp are not, they emit byte
 * accesses and fault. Word-wise equivalents, used for IRAM chunks.
 *
 * The exception stream (uint8) and act[] (uint16) are byte- and half-word
 * addressed, so neither is ever eligible for IRAM. They go to DRAM or stay in
 * flash. */
static void wcopy(uint32_t *d, const uint32_t *s, size_t bytes) {
    for (size_t w = 0; w < bytes / 4; w++) d[w] = s[w];
}
static int wcmp(const uint32_t *a, const uint32_t *b, size_t bytes) {
    for (size_t w = 0; w < bytes / 4; w++) if (a[w] != b[w]) return 1;
    return 0;
}

/* Allocate a chunk from the IRAM-ONLY pool, or return NULL.
 *
 * This matters more than it looks. MALLOC_CAP_EXEC also draws from the D/IRAM
 * regions, which are ordinary DRAM to everything else — including the mbedTLS
 * handshake. An earlier version used it unguarded and ran the board down to
 * 15,900 B free, which would have died on the first HTTPS request.
 *
 * The guard is not an address-range test (those are chip-specific and easy to
 * get subtly wrong). It asks the question we actually care about directly: did
 * this allocation consume any of the 8-bit-capable pool? If it did, it is
 * competing with TLS, so give it back. */
static void *iram_only_malloc(size_t bytes) {
    size_t before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    void *p = heap_caps_malloc(bytes, MALLOC_CAP_EXEC | MALLOC_CAP_32BIT);
    if (!p) return NULL;
    if (heap_caps_get_free_size(MALLOC_CAP_8BIT) < before) {
        heap_caps_free(p);                   /* came out of DRAM; not free money */
        return NULL;
    }
    return p;
}

int lift_run(lift_t *L, const uint32_t *mask, const uint16_t *act,
             const uint16_t *eoff, const uint8_t *epos, uint32_t nex,
             uint32_t n_index, size_t reserve)
{
    memset(L, 0, sizeof *L);
    L->act = act; L->eoff = eoff; L->epos = epos;
    L->n_index = n_index; L->nex = nex;
    if (n_index > (uint32_t)LIFT_MAXCHUNK * LIFT_CHUNK_VECS) return -1;
    L->nch     = (n_index + LIFT_CHUNK_VECS - 1) / LIFT_CHUNK_VECS;
    L->need    = (size_t)n_index * RMASKB + (size_t)n_index * 2
               + ((size_t)n_index + 1) * 2 + nex;
    /* esp_get_free_heap_size() is the SUM across heap regions; the ESP32 splits
       DRAM into non-contiguous blocks, so a single malloc is bounded by the
       LARGEST block. That distinction is why a flat lift can never succeed. */
    L->largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

    /* The small per-vector sidecars first: act[], the offset table, and the
       exception bytes. Together these are ~13 KB for the shipped index and are
       touched once per vector exactly as the masks are, so they earn SRAM at
       least as much as any chunk does. */
    {   size_t abytes = (size_t)n_index * 2;
        if (heap_caps_get_free_size(MALLOC_CAP_8BIT) > abytes + reserve) {
            uint16_t *ra = malloc(abytes);
            if (ra) { memcpy(ra, act, abytes); L->act = ra; }
        }
        size_t obytes = ((size_t)n_index + 1) * 2;
        if (heap_caps_get_free_size(MALLOC_CAP_8BIT) > obytes + reserve) {
            uint16_t *ro = malloc(obytes);
            if (ro) { memcpy(ro, eoff, obytes); L->eoff = ro; }
        }
        if (nex && heap_caps_get_free_size(MALLOC_CAP_8BIT) > nex + reserve) {
            uint8_t *re = malloc(nex);
            if (re) { memcpy(re, epos, nex); L->epos = re; }
        }
    }

    int iram_exhausted = 0;
    for (uint32_t c = 0; c < L->nch; c++) {
        uint32_t off = c * LIFT_CHUNK_VECS;
        uint32_t n   = n_index - off; if (n > LIFT_CHUNK_VECS) n = LIFT_CHUNK_VECS;
        size_t   b   = (size_t)n * RMASKB;
        const uint32_t *src = mask + (size_t)off * RWORDS;
        L->ch[c] = src;                      /* flash by default */

        uint32_t *dst = NULL; int is_iram = 0;
        if (heap_caps_get_free_size(MALLOC_CAP_8BIT) >= b + reserve)
            dst = malloc(b);
        if (!dst && !iram_exhausted) {
            dst = iram_only_malloc(b);
            if (dst) is_iram = 1; else iram_exhausted = 1;
        }
        if (!dst) continue;                  /* stays flash-mapped, scores the same */

        if (is_iram) wcopy(dst, src, b);
        else         memcpy(dst, src, b);
        L->ch[c] = dst;
        L->vec_dram += is_iram ? 0 : n;
        L->vec_iram += is_iram ? n : 0;
    }

    /* Verify the CHUNK ADDRESSING, not just the copy. memcmp'ing a chunk right
       after memcpy'ing it re-reads what it just wrote and cannot fail. The real
       hazard is an off-by-one at a chunk boundary, which would silently score
       vector i against another vector's bytes. Walk every index through the
       exact nested accessor the scan uses. */
    {
        uint32_t i = 0;
        for (uint32_t c = 0; c < L->nch; c++) {
            const uint32_t *base = L->ch[c];
            uint32_t n = n_index - i; if (n > LIFT_CHUNK_VECS) n = LIFT_CHUNK_VECS;
            for (uint32_t k = 0; k < n; k++, i++)
                if (wcmp(base + (size_t)k * RWORDS,
                         mask + (size_t)i * RWORDS, RMASKB)) L->bad++;
        }
        if (i != n_index) L->bad++;          /* the walk must cover the index exactly */
    }
    return 0;
}
