#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../src/mogwai_method.h"

static int expect_reject(const router_t *r, uint8_t *wire, size_t n) {
    mog_artifact_t a;
    return mog_parse_wire(r, wire, n, &a) != 0;
}

static size_t load_wire(const char *path, uint8_t *wire, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long n = ftell(f);
    if (n < 0 || (size_t)n > cap) { fclose(f); return 0; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }
    size_t got = fread(wire, 1, (size_t)n, f);
    int ok = fclose(f) == 0;
    return ok && got == (size_t)n ? got : 0;
}

int main(int argc, char **argv) {
    if (argc > 2) {
        fprintf(stderr, "usage: %s [artifact.mog1]\n", argv[0]);
        return 1;
    }
    router_t r = {0};
    r.dim = RD;
    uint8_t wire[MOG_WIRE_MAX_BYTES];
    size_t n = argc == 2 ? load_wire(argv[1], wire, sizeof wire)
                         : mog_build_log_triage_wire(wire, sizeof wire);
    if (!n) return 2;
    int checks = 0, pass = 0;
    mog_artifact_t a;
    checks++;
    if (n == 3180 && mog_parse_wire(&r, wire, n, &a) == 0 && mog_artifact_valid(&a)) pass++;

    uint8_t bad[MOG_WIRE_MAX_BYTES];
    memcpy(bad, wire, n);
    ((mog_wire_header_t *)bad)->magic ^= 1u; checks++;
    if (expect_reject(&r, bad, n)) pass++;

    checks++;
    if (expect_reject(&r, wire, n - 1)) pass++;

    memcpy(bad, wire, n);
    ((mog_wire_header_t *)bad)->nex = MOG_LOG_TRIAGE_TRAIN_CAP + 1; checks++;
    if (expect_reject(&r, bad, n)) pass++;

    memcpy(bad, wire, n);
    ((mog_wire_header_t *)bad)->selected_threshold = 109; checks++;
    if (expect_reject(&r, bad, n)) pass++;

    memcpy(bad, wire, n);
    mog_wire_case_t *cases = (mog_wire_case_t *)(bad + sizeof(mog_wire_header_t));
    cases[0].negative = 2; checks++;
    if (expect_reject(&r, bad, n)) pass++;

    memcpy(bad, wire, n);
    cases = (mog_wire_case_t *)(bad + sizeof(mog_wire_header_t));
    cases[0].intent = 99; checks++;
    if (expect_reject(&r, bad, n)) pass++;

    memcpy(bad, wire, n);
    ((mog_wire_header_t *)bad)->schema_name[0] = 'x'; checks++;
    if (expect_reject(&r, bad, n)) pass++;

    memcpy(bad, wire, n);
    cases = (mog_wire_case_t *)(bad + sizeof(mog_wire_header_t));
    memset(cases[0].text, 'x', sizeof cases[0].text); checks++;
    if (expect_reject(&r, bad, n)) pass++;

    printf("MOGWAI_METHOD_GUARD checks=%d/%d\n", pass, checks);
    return pass == checks ? 0 : 1;
}
