#include "../src/runtime_choice.h"
#include <stdio.h>
#include <string.h>

static int total, pass;

static void chk(const char *name, int ok) {
    total++;
    if (ok) pass++;
    else printf("  FAIL runtime_choice_device: %s\n", name);
}

static runtime_candidate_t cand(const char *text, uint64_t code, int loc) {
    runtime_candidate_t c;
    memset(&c, 0, sizeof c);
    c.text = text;
    c.sem_code = code;
    c.factor_flags = RTC_FACTOR_POLARITY|RTC_FACTOR_COMPOSITION|RTC_FACTOR_LOCATION|RTC_FACTOR_SUPPORT;
    c.polarity = RTC_POLARITY_POSITIVE;
    c.composition = RTC_COMP_LIGHTING|RTC_COMP_ACTIVATION;
    c.location_id = (uint8_t)loc;
    c.support = RTC_SUPPORT_SUPPORTED;
    return c;
}

int main(void) {
    runtime_candidate_t q = cand("activate hallway lamps", 0x1555555555555555ull, 1);
    runtime_candidate_t c[RUNTIME_CHOICE_MAX_CANDIDATES];
    char text[RUNTIME_CHOICE_MAX_CANDIDATES][32];
    uint8_t blob[RTC_CAND_MAX_BYTES];
    runtime_candidate_t parsed[RUNTIME_CHOICE_MAX_CANDIDATES];
    runtime_choice_t a, b;
    runtime_factor_reason_t fa, fb;
    size_t written = 0;
    int parsed_n = -1;

    for (int i = 0; i < RUNTIME_CHOICE_MAX_CANDIDATES; i++) {
        snprintf(text[i], sizeof text[i], "candidate %02d", i);
        c[i] = cand(text[i], 0xaaaaaaaaaaaaaaaaull ^ (uint64_t)i, 1);
    }
    c[17].sem_code = q.sem_code;

    chk("worst-case candidate cap pinned", RUNTIME_CHOICE_MAX_CANDIDATES == 32);
    chk("worst-case serialized bytes pinned", RTC_CAND_MAX_BYTES == 9000u);
    chk("worst-case operation budget pinned", RTC_SCORE_MAX_BIT_COMPARISONS == 2048);
    chk("worst-case flat scorer selects exact code", r_choose_runtime_precomputed(&q, 64, c, RUNTIME_CHOICE_MAX_CANDIDATES, &a, &fa) == 0 && a.reason == RTC_REASON_OK && a.winner == 17 && a.score == 656 && a.second == 192 && a.margin == 464 && fa == RTC_FACTOR_REASON_OK);
    chk("worst-case RTC1 writes", r_runtime_write_candidates(blob, sizeof blob, c, RUNTIME_CHOICE_MAX_CANDIDATES, &written) == 0 && written == RTC_CAND_MAX_BYTES);
    chk("worst-case RTC1 parses", r_runtime_parse_candidates(blob, written, parsed, RUNTIME_CHOICE_MAX_CANDIDATES, &parsed_n) == 0 && parsed_n == RUNTIME_CHOICE_MAX_CANDIDATES);
    chk("parsed scoring parity exact", r_choose_runtime_precomputed(&q, 64, parsed, parsed_n, &b, &fb) == 0 && b.winner == a.winner && b.score == a.score && b.second == a.second && b.margin == a.margin && b.reason == a.reason && fb == fa);

    parsed[17].location_id = 2;
    chk("device path preserves factor refusal", r_choose_runtime_precomputed(&q, 64, parsed, parsed_n, &b, &fb) == 0 && b.reason == RTC_REASON_OK && b.winner != 17 && fb == RTC_FACTOR_REASON_OK);
    for (int i = 0; i < RUNTIME_CHOICE_MAX_CANDIDATES; i++) parsed[i].location_id = 2;
    chk("device path all-factor-reject attribution", r_choose_runtime_precomputed(&q, 64, parsed, parsed_n, &b, &fb) == 0 && b.reason == RTC_REASON_FACTOR_REJECT && b.winner == -1 && fb == RTC_FACTOR_REASON_LOCATION);

    printf("RUNTIME_CHOICE_DEVICE checks=%d/%d\n", pass, total);
    return pass == total ? 0 : 1;
}
