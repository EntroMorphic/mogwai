/* Validates router.bin against the layout documented in doc/BLOB_FORMAT.md.
 * Parses strictly, section by section, and requires that the last reference
 * record ends EXACTLY at EOF — a doc that drifts from the writer shows up here
 * as leftover or missing bytes rather than as a mystery parity failure. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/router.h"
#include "../src/ternary.h"

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: blobfmt router.bin\n"); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); long size = ftell(f); rewind(f);
    unsigned char *b = malloc(size);
    if (fread(b, 1, size, f) != (size_t)size) { fprintf(stderr, "short read\n"); return 1; }
    fclose(f);

    long o = 0;
    uint32_t hdr[5]; memcpy(hdr, b + o, 20); o += 20;
    printf("  header      @%-8ld magic=%08x dim=%u n_index=%u n_class=%u threshold=%u\n",
           0L, hdr[0], hdr[1], hdr[2], hdr[3], hdr[4]);
    if (hdr[0] == RMAGIC) {
        printf("  FAIL: this is a v1 blob ('RTR1'); the runtime is v2 — rebuild it\n");
        return 1; }
    if (hdr[0] != RMAGIC2) { printf("  FAIL: magic mismatch (want %08x)\n", RMAGIC2); return 1; }
    if (hdr[1] != (uint32_t)RD) { printf("  FAIL: blob dim %u != build RD %d\n", hdr[1], RD); return 1; }
    uint32_t n = hdr[2];

    printf("  names       @%-8ld %d x %d = %d B\n", o, RMAXCLS, RNAMELEN, RMAXCLS*RNAMELEN);
    o += (long)RMAXCLS * RNAMELEN;
    printf("  centre      @%-8ld %d x int32 = %d B\n", o, RD, RD*4);
    o += (long)RD * 4;
    /* Sections run in descending alignment order so each lands aligned for any
       n; blobfmt asserts that rather than trusting it. */
    if (o % 4) { printf("  FAIL: masks at %ld are not 4-aligned\n", o); return 1; }
    printf("  masks       @%-8ld %u x %d B = %ld B\n", o, n, RMASKB, (long)n*RMASKB);
    o += (long)n * RMASKB;
    if (o % 2) { printf("  FAIL: eoff at %ld is not 2-aligned\n", o); return 1; }
    const uint16_t *eoff = (const uint16_t *)(const void *)(b + o);
    printf("  eoff        @%-8ld %u x uint16\n", o, n + 1);
    o += ((long)n + 1) * 2;
    if (o % 2) { printf("  FAIL: act at %ld is not 2-aligned\n", o); return 1; }
    printf("  act         @%-8ld %u x uint16\n", o, n);  o += (long)n * 2;
    printf("  label       @%-8ld %u x uint8\n", o, n);   o += n;
    for (uint32_t i = 0; i < n; i++)
        if (eoff[i] > eoff[i+1]) {
            printf("  FAIL: eoff not ascending at %u (%u > %u)\n", i, eoff[i], eoff[i+1]);
            return 1; }
    uint32_t nex = eoff[n];
    printf("  epos        @%-8ld %u x uint8  (%.3f%% of %u dims are sign exceptions)\n",
           o, nex, 100.0*nex/((double)n*RD), n*RD);
    o += nex;
    uint32_t nref; memcpy(&nref, b + o, 4); o += 4;
    printf("  nref        @%-8ld %u\n", o - 4, nref);
    if (nref > 4096) { printf("  FAIL: implausible nref\n"); return 1; }

    long refstart = o;
    for (uint32_t i = 0; i < nref; i++) {
        if (o + 1 > size) { printf("  FAIL: ran off the end at record %u\n", i); return 1; }
        uint8_t len = b[o++];
        if (o + len + 5 > size) { printf("  FAIL: record %u overruns EOF\n", i); return 1; }
        o += len + 4 + 1;                       /* text, int32 score, int8 class */
    }
    printf("  refs        @%-8ld %u records, %ld B (avg text %.1f)\n",
           refstart, nref, o - refstart, (double)(o - refstart - 6*nref) / nref);
    printf("  ---\n  parsed %ld of %ld bytes\n", o, size);
    if (o != size) { printf("  FAIL: %ld bytes unaccounted for — doc and writer disagree\n", size - o); return 1; }
    printf("  OK: layout matches doc/BLOB_FORMAT.md exactly\n");
    return 0;
}
