/* blobguard — unit test for r_parse2(), the validating parser the FIRMWARE
 * uses. blobfmt checks the layout offline; this checks that a corrupt blob is
 * REFUSED rather than misread, one invariant at a time.
 *
 * Every case here was found by red-teaming the v2 format, and two of them were
 * live gaps at the time:
 *   - an unsorted exception slice was ACCEPTED, and silently returned a wrong
 *     dot (measured 6 -> 2, score 109 -> 36) because t_dot_ex merges the lists
 *   - a blob truncated by ONE byte was ACCEPTED, because the reference records
 *     were never bounded
 * A test that only ever sees a good blob would have caught neither. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/router.h"
#include "../src/ternary.h"

static unsigned char *ORIG; static long SZ;
static int fails = 0;

static void expect(const char *what, unsigned char *b, size_t have, int want) {
    router_t R; rindex2 IX;
    int rc = r_parse2(&R, &IX, b, have);
    int ok = want ? (rc == want) : (rc == 0);
    printf("  %-46s rc=%-3d %s\n", what, rc, ok ? "ok" : "*** WRONG ***");
    if (!ok) fails++;
}
static unsigned char *copy(void) {
    unsigned char *b = malloc(SZ); memcpy(b, ORIG, SZ); return b;
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "esp32_router/main/router.bin";
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 1; }
    fseek(f, 0, SEEK_END); SZ = ftell(f); rewind(f);
    ORIG = malloc(SZ);
    if (fread(ORIG, 1, SZ, f) != (size_t)SZ) { fprintf(stderr, "short read\n"); return 1; }
    fclose(f);

    printf("\n  === r_parse2 guard (%ld bytes) ===\n", SZ);
    expect("the shipped blob is accepted", ORIG, SZ, 0);

    {   unsigned char *b = copy(); b[0] ^= 0xff;
        expect("bad magic is refused", b, SZ, -1); free(b); }
    {   unsigned char *b = copy(); uint32_t d = RD + 1; memcpy(b + 4, &d, 4);
        expect("wrong dim is refused", b, SZ, -1); free(b); }
    {   unsigned char *b = copy(); uint32_t z = 0; memcpy(b + 8, &z, 4);
        expect("zero n_index is refused", b, SZ, -2); free(b); }
    {   unsigned char *b = copy(); uint32_t huge = 1u << 20; memcpy(b + 8, &huge, 4);
        expect("an n_index past the extent is refused", b, SZ, -2); free(b); }

    /* Truncation: any refusal is correct. Below 20 bytes there is not even a
       header, so the parser says -1 rather than -2; demanding a specific code
       here tests the error taxonomy, not the guard. */
    for (long cut = 10; cut < SZ; cut = cut * 3 / 2 + 1) {
        router_t R; rindex2 IX;
        int rc = r_parse2(&R, &IX, ORIG, (size_t)cut);
        if (rc == 0) { printf("  truncated to %-9ld *** ACCEPTED ***\n", cut); fails++; }
    }
    printf("  %-46s %s\n", "every truncation from 10 bytes up is refused",
           fails ? "*** SOME ACCEPTED ***" : "ok");
    {   router_t R; rindex2 IX;
        int rc = r_parse2(&R, &IX, ORIG, (size_t)SZ - 1);
        int ok = rc != 0;
        printf("  %-46s rc=%-3d %s\n", "truncated by ONE byte is refused", rc,
               ok ? "ok" : "*** WRONG ***");
        if (!ok) fails++; }

    /* Structural corruption. NOTE: IX is parsed from the COPY, so its pointers
       already point into the copy — an earlier version of this test subtracted
       ORIG and added b, corrupting a random address and "proving" the guard
       worked when it had not run at all. */
    {   router_t R; rindex2 IX;
        unsigned char *b = copy();
        r_parse2(&R, &IX, b, SZ);
        uint16_t *eoff = (uint16_t *)(void *)IX.eoff;
        uint32_t at = 0;
        for (uint32_t i = 0; i + 1 < R.n_index; i++)
            if (eoff[i] != eoff[i+1]) { at = i; break; }
        if (at) { uint16_t t = eoff[at]; eoff[at] = eoff[at+1]; eoff[at+1] = t;
                  expect("non-ascending offsets are refused", b, SZ, -3); }
        else printf("  (no differing adjacent offsets; check skipped)\n");
        free(b); }

    {   router_t R; rindex2 IX;
        unsigned char *b = copy();
        r_parse2(&R, &IX, b, SZ);
        uint32_t v = 0; int n = 0;
        for (uint32_t i = 0; i < R.n_index; i++) {
            int c = IX.eoff[i+1] - IX.eoff[i];
            if (c >= 2) { v = i; n = c; break; } }
        if (n >= 2) {
            uint8_t *slice = (uint8_t *)IX.epos + IX.eoff[v];
            uint8_t p0 = slice[0], p1 = slice[1];
            slice[0] = p1; slice[1] = p0;
            expect("an unsorted exception slice is refused", b, SZ, -5);
            slice[0] = p0; slice[1] = p0;
            expect("a duplicated exception position is refused", b, SZ, -5);
        } else printf("  (no vector with >=2 exceptions; slice checks skipped)\n");
        free(b); }

    {   router_t R; rindex2 IX;
        unsigned char *b = copy();
        r_parse2(&R, &IX, b, SZ);
        uint8_t *nref = (uint8_t *)IX.refp - 4;
        uint32_t many = IX.nref + 64; memcpy(nref, &many, 4);
        expect("reference records past the end are refused", b, SZ, -6);
        free(b); }
    printf("\n  %s (%d failures)\n", fails ? "*** BLOBGUARD FAILED ***" : "blobguard OK", fails);
    return fails ? 1 : 0;
}
