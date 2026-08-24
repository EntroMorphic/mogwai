/* imgcheck — does the flashable image actually contain the blob we ship?
 *
 * The release pipeline builds firmware, embeds router.bin, and merges the
 * result into one file a stranger will write to their board. Nothing in that
 * chain verifies that the data which came out is the data that went in: a stale
 * build directory, a partial rebuild, or a merge that picked up the wrong app
 * would all produce a plausible image of exactly the right size.
 *
 * So: find the blob inside the image, byte for byte. Not a checksum of the
 * image (which tells you nothing about its contents), and not the header alone
 * (which survives a truncated or corrupted body). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb");
    if (!f) { perror(p); exit(2); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    unsigned char *b = malloc((size_t)sz);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) { fprintf(stderr, "short read %s\n", p); exit(2); }
    fclose(f); *n = (size_t)sz; return b;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: imgcheck <image.bin> <router.bin>\n"
                        "  verifies the blob appears verbatim inside the image\n");
        return 2;
    }
    size_t in, bn;
    unsigned char *img = slurp(argv[1], &in);
    unsigned char *blob = slurp(argv[2], &bn);
    if (bn > in) { printf("  FAIL: blob (%zu B) is larger than the image (%zu B)\n", bn, in); return 1; }

    /* plain search: the image is a few hundred KB, this is instant and has no
       dependency on memmem being available */
    size_t at = (size_t)-1, hits = 0;
    for (size_t i = 0; i + bn <= in; i++)
        if (img[i] == blob[0] && !memcmp(img + i, blob, bn)) { if (at == (size_t)-1) at = i; hits++; }

    printf("  image %zu B, blob %zu B\n", in, bn);
    if (!hits) { printf("  *** FAIL: the blob is NOT present in the image ***\n"); return 1; }
    printf("  blob found verbatim at 0x%zx (%zu occurrence%s)\n", at, hits, hits == 1 ? "" : "s");
    return 0;
}
