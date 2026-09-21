#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "mogwai_method.h"

static int read_file(const char *path, uint8_t *buf, size_t cap, size_t *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long n = ftell(f);
    if (n < 0 || (size_t)n > cap) { fclose(f); return 0; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }
    size_t got = fread(buf, 1, (size_t)n, f);
    int ok = fclose(f) == 0;
    if (!ok || got != (size_t)n) return 0;
    *out = got;
    return 1;
}

int main(int argc, char **argv) {
    int check = argc == 3 && !strcmp(argv[1], "--check");
    const char *path = check ? argv[2] : (argc == 2 ? argv[1] : "results/log_triage.mog1");
    if (argc > 3 || (argc == 3 && !check)) {
        fprintf(stderr, "usage: %s [out.mog1] | --check artifact.mog1\n", argv[0]);
        return 1;
    }
    uint8_t wire[MOG_WIRE_MAX_BYTES];
    size_t n = mog_build_log_triage_wire(wire, sizeof wire);
    if (!n) return 2;
    if (check) {
        uint8_t existing[MOG_WIRE_MAX_BYTES];
        size_t have = 0;
        if (!read_file(path, existing, sizeof existing, &have)) return 3;
        if (have != n || memcmp(existing, wire, n)) return 4;
        printf("MOGWAI_METHOD_BUILD_CHECK path=%s bytes=%lu\n", path, (unsigned long)n);
        return 0;
    }
    FILE *f = fopen(path, "wb");
    if (!f) return 3;
    int ok = fwrite(wire, 1, n, f) == n;
    ok = fclose(f) == 0 && ok;
    if (!ok) return 4;
    printf("MOGWAI_METHOD_BUILD path=%s bytes=%lu\n", path, (unsigned long)n);
    return 0;
}
