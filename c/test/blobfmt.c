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
    if (hdr[0] != RMAGIC)  { printf("  FAIL: magic mismatch (want %08x)\n", RMAGIC); return 1; }
    if (hdr[1] != (uint32_t)RD) { printf("  FAIL: blob dim %u != build RD %d\n", hdr[1], RD); return 1; }
    uint32_t n = hdr[2];

    printf("  names       @%-8ld %d x %d = %d B\n", o, RMAXCLS, RNAMELEN, RMAXCLS*RNAMELEN);
    o += (long)RMAXCLS * RNAMELEN;
    printf("  centre      @%-8ld %d x int32 = %d B\n", o, RD, RD*4);
    o += (long)RD * 4;
    printf("  label       @%-8ld %u x uint8\n", o, n);   o += n;
    printf("  act         @%-8ld %u x uint16\n", o, n);  o += (long)n * 2;
    printf("  index       @%-8ld %u x tvec(%zu B)\n", o, n, sizeof(tvec));
    o += (long)n * sizeof(tvec);
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
