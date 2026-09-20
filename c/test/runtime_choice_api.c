#include "../src/runtime_choice.h"
#include <stdio.h>
#include <string.h>

static int total, pass;
static void chk(const char *name, int ok) {
    total++;
    if (ok) pass++;
    else printf("  FAIL runtime_choice_api: %s\n", name);
}

static int same_none(runtime_choice_t a, runtime_choice_t b) {
    return a.winner == -1 && b.winner == -1 &&
           a.score == b.score && a.second == b.second &&
           a.margin == b.margin && a.reason == b.reason;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void put64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) { p[i] = (uint8_t)v; v >>= 8; } }

static void make_blob(uint8_t *blob, int n) {
    memset(blob, 0, 8 + (size_t)n * RTC_CAND_RECORD_BYTES);
    put32(blob, RTC_CAND_MAGIC);
    put32(blob + 4, (uint32_t)n);
    for (int i = 0; i < n; i++) {
        uint8_t *p = blob + 8 + (size_t)i * RTC_CAND_RECORD_BYTES;
        put64(p, (uint64_t)(100 + i));
        put32(p + 8, (uint32_t)(10 + i));
        put32(p + 12, RTC_FACTOR_SUPPORT);
        p[20] = RTC_SUPPORT_SUPPORTED;
        snprintf((char *)(void *)(p + 24), RTC_CAND_TEXT_BYTES, "candidate %d", i);
    }
}

int main(void) {
    router_t r;
    runtime_choice_t out;
    runtime_choice_t out2;
    runtime_candidate_t one[1] = {{"make coffee", 0, 0, 0, 0, 0, 0, 0, 0}};
    runtime_candidate_t dup[2] = {{"make coffee", 1, 2, RTC_FACTOR_POLARITY, 1, 0, 0, 0, 0}, {"make coffee", 1, 2, RTC_FACTOR_POLARITY, 1, 0, 0, 0, 0}};
    runtime_candidate_t perm_a[2] = {{"make coffee", 1, 2, RTC_FACTOR_POLARITY, 1, 0, 0, 0, 0}, {"clean the flat", 3, 4, RTC_FACTOR_SUPPORT, 0, 0, 0, 0, RTC_SUPPORT_SUPPORTED}};
    runtime_candidate_t perm_b[2] = {{"clean the flat", 3, 4, RTC_FACTOR_SUPPORT, 0, 0, 0, 0, RTC_SUPPORT_SUPPORTED}, {"make coffee", 1, 2, RTC_FACTOR_POLARITY, 1, 0, 0, 0, 0}};
    runtime_candidate_t code_cands[3] = {{"a", 0x0, 1000, 0, 0, 0, 0, 0, 0}, {"b", 0x1, -1000, 0, 0, 0, 0, 0, 0}, {"c", 0x3, 1000, 0, 0, 0, 0, 0, 0}};
    runtime_candidate_t empty_text[1] = {{"", 0, 0, 0, 0, 0, 0, 0, 0}};
    runtime_candidate_t bad_flags[1] = {{"make coffee", 0, 0, 0x80000000u, 0, 0, 0, 0, 0}};
    runtime_candidate_t bad_pol[1] = {{"make coffee", 0, 0, RTC_FACTOR_POLARITY, 2, 0, 0, 0, 0}};
    runtime_candidate_t bad_color[1] = {{"make coffee", 0, 0, RTC_FACTOR_COLOR, 0, 99, 0, 0, 0}};
    runtime_candidate_t bad_comp[1] = {{"make coffee", 0, 0, RTC_FACTOR_COMPOSITION, 0, 0, 0x80, 0, 0}};
    runtime_candidate_t bad_support[1] = {{"make coffee", 0, 0, RTC_FACTOR_SUPPORT, 0, 0, 0, 0, 99}};
    runtime_candidate_t hidden_pol[1] = {{"make coffee", 0, 0, 0, 1, 0, 0, 0, 0}};
    runtime_candidate_t empty_pol[1] = {{"make coffee", 0, 0, RTC_FACTOR_POLARITY, 0, 0, 0, 0, 0}};
    runtime_candidate_t hidden_color[1] = {{"make coffee", 0, 0, 0, 0, RTC_COLOR_RED, 0, 0, 0}};
    runtime_candidate_t hidden_comp[1] = {{"make coffee", 0, 0, 0, 0, 0, RTC_COMP_LIGHTING, 0, 0}};
    runtime_candidate_t hidden_loc[1] = {{"make coffee", 0, 0, 0, 0, 0, 0, 7, 0}};
    runtime_candidate_t hidden_support[1] = {{"make coffee", 0, 0, 0, 0, 0, 0, 0, RTC_SUPPORT_SUPPORTED}};
    runtime_candidate_t many[RUNTIME_CHOICE_MAX_CANDIDATES + 1];
    char too_long[RUNTIME_CHOICE_MAX_TEXT + 2];
    uint8_t blob[8 + 2 * RTC_CAND_RECORD_BYTES + 1];
    uint8_t blob2[8 + 2 * RTC_CAND_RECORD_BYTES];
    runtime_candidate_t parsed[2];
    int parsed_n = -1;
    size_t written = 999;
    int32_t score = 12345;
    runtime_factor_reason_t freason = RTC_FACTOR_REASON_BAD_ARGUMENT;

    memset(&r, 0, sizeof r);
    memset(many, 0, sizeof many);
    for (int i = 0; i <= RUNTIME_CHOICE_MAX_CANDIDATES; i++) many[i].text = "x";
    memset(too_long, 'a', sizeof too_long);
    too_long[sizeof too_long - 1] = 0;

    chk("null output rejected", r_choose_runtime(&r, "x", one, 1, NULL) == -1);
    chk("null router rejected", r_choose_runtime(NULL, "x", one, 1, &out) == -1 && out.reason == RTC_REASON_BAD_ARGUMENT && out.winner == -1);
    chk("null query rejected", r_choose_runtime(&r, NULL, one, 1, &out) == -1 && out.reason == RTC_REASON_BAD_ARGUMENT && out.winner == -1);
    chk("negative count rejected", r_choose_runtime(&r, "x", one, -1, &out) == -1 && out.reason == RTC_REASON_BAD_ARGUMENT && out.winner == -1);
    chk("empty candidate set abstains", r_choose_runtime(&r, "x", NULL, 0, &out) == 0 && out.reason == RTC_REASON_NONE_NO_CANDIDATES && out.winner == -1);
    chk("null candidates rejected", r_choose_runtime(&r, "x", NULL, 1, &out) == -1 && out.reason == RTC_REASON_BAD_ARGUMENT && out.winner == -1);
    chk("maximum candidate count accepted", r_choose_runtime(&r, "x", many, RUNTIME_CHOICE_MAX_CANDIDATES, &out) == 0 && out.reason == RTC_REASON_UNSUPPORTED_SCORER && out.winner == -1);
    chk("too many candidates rejected", r_choose_runtime(&r, "x", many, RUNTIME_CHOICE_MAX_CANDIDATES + 1, &out) == -1 && out.reason == RTC_REASON_TOO_MANY_CANDIDATES && out.winner == -1);
    chk("missing candidate text rejected", r_choose_runtime(&r, "x", &(runtime_candidate_t){0}, 1, &out) == -1 && out.reason == RTC_REASON_MALFORMED_CANDIDATE && out.winner == -1);
    chk("empty candidate text rejected", r_choose_runtime(&r, "x", empty_text, 1, &out) == -1 && out.reason == RTC_REASON_MALFORMED_CANDIDATE && out.winner == -1);
    chk("unknown factor flags rejected", r_choose_runtime(&r, "x", bad_flags, 1, &out) == -1 && out.reason == RTC_REASON_MALFORMED_CANDIDATE && out.winner == -1);
    chk("bad polarity rejected", r_choose_runtime(&r, "x", bad_pol, 1, &out) == -1 && out.reason == RTC_REASON_MALFORMED_CANDIDATE && out.winner == -1);
    chk("bad color rejected", r_choose_runtime(&r, "x", bad_color, 1, &out) == -1 && out.reason == RTC_REASON_MALFORMED_CANDIDATE && out.winner == -1);
    chk("bad composition rejected", r_choose_runtime(&r, "x", bad_comp, 1, &out) == -1 && out.reason == RTC_REASON_MALFORMED_CANDIDATE && out.winner == -1);
    chk("bad support rejected", r_choose_runtime(&r, "x", bad_support, 1, &out) == -1 && out.reason == RTC_REASON_MALFORMED_CANDIDATE && out.winner == -1);
    chk("hidden polarity rejected", r_choose_runtime(&r, "x", hidden_pol, 1, &out) == -1 && out.reason == RTC_REASON_MALFORMED_CANDIDATE && out.winner == -1);
    chk("empty polarity flag rejected", r_choose_runtime(&r, "x", empty_pol, 1, &out) == -1 && out.reason == RTC_REASON_MALFORMED_CANDIDATE && out.winner == -1);
    chk("hidden color rejected", r_choose_runtime(&r, "x", hidden_color, 1, &out) == -1 && out.reason == RTC_REASON_MALFORMED_CANDIDATE && out.winner == -1);
    chk("hidden composition rejected", r_choose_runtime(&r, "x", hidden_comp, 1, &out) == -1 && out.reason == RTC_REASON_MALFORMED_CANDIDATE && out.winner == -1);
    chk("hidden location rejected", r_choose_runtime(&r, "x", hidden_loc, 1, &out) == -1 && out.reason == RTC_REASON_MALFORMED_CANDIDATE && out.winner == -1);
    chk("hidden support rejected", r_choose_runtime(&r, "x", hidden_support, 1, &out) == -1 && out.reason == RTC_REASON_MALFORMED_CANDIDATE && out.winner == -1);
    chk("empty query rejected", r_choose_runtime(&r, "", one, 1, &out) == -1 && out.reason == RTC_REASON_MALFORMED_QUERY && out.winner == -1);
    chk("overlong query rejected", r_choose_runtime(&r, too_long, one, 1, &out) == -1 && out.reason == RTC_REASON_MALFORMED_QUERY && out.winner == -1);
    chk("overlong candidate rejected", r_choose_runtime(&r, "x", &(runtime_candidate_t){too_long, 0, 0, 0, 0, 0, 0, 0, 0}, 1, &out) == -1 && out.reason == RTC_REASON_MALFORMED_CANDIDATE && out.winner == -1);
    chk("duplicate candidates accepted before scoring", r_choose_runtime(&r, "brew espresso", dup, 2, &out) == 0 && out.reason == RTC_REASON_UNSUPPORTED_SCORER && out.winner == -1);
    chk("non-scoring outcome order invariant", r_choose_runtime(&r, "brew espresso", perm_a, 2, &out) == 0 && r_choose_runtime(&r, "brew espresso", perm_b, 2, &out2) == 0 && same_none(out, out2));
    chk("reason names are stable", !strcmp(r_runtime_reason_name(RTC_REASON_UNSUPPORTED_SCORER), "unsupported_scorer") && !strcmp(r_runtime_reason_name((runtime_choice_reason_t)999), "unknown"));
    chk("valid scaffold fails closed", r_choose_runtime(&r, "brew espresso", one, 1, &out) == 0 && out.reason == RTC_REASON_UNSUPPORTED_SCORER && out.winner == -1 && out.score == 0 && out.margin == 0);

    make_blob(blob, 2);
    chk("candidate blob parses", r_runtime_parse_candidates(blob, 8 + 2 * RTC_CAND_RECORD_BYTES, parsed, 2, &parsed_n) == 0 && parsed_n == 2 && !strcmp(parsed[1].text, "candidate 1") && parsed[1].support == RTC_SUPPORT_SUPPORTED);
    chk("candidate blob exact EOF", r_runtime_parse_candidates(blob, 8 + 2 * RTC_CAND_RECORD_BYTES + 1, parsed, 2, &parsed_n) == -2 && parsed_n == 0);
    blob[0] ^= 1;
    chk("candidate blob magic rejects", r_runtime_parse_candidates(blob, 8 + 2 * RTC_CAND_RECORD_BYTES, parsed, 2, &parsed_n) == -2 && parsed_n == 0);
    make_blob(blob, 2);
    put32(blob + 4, RUNTIME_CHOICE_MAX_CANDIDATES + 1);
    chk("candidate blob count rejects", r_runtime_parse_candidates(blob, sizeof blob, parsed, 2, &parsed_n) == -2 && parsed_n == 0);
    make_blob(blob, 1);
    blob[8 + 21] = 1;
    chk("candidate blob reserved bytes reject", r_runtime_parse_candidates(blob, 8 + RTC_CAND_RECORD_BYTES, parsed, 2, &parsed_n) == -3 && parsed_n == 0);
    make_blob(blob, 1);
    blob[8 + 24] = 0;
    chk("candidate blob empty text rejects", r_runtime_parse_candidates(blob, 8 + RTC_CAND_RECORD_BYTES, parsed, 2, &parsed_n) == -3 && parsed_n == 0);
    make_blob(blob, 1);
    memset(blob + 8 + 24, 'x', RTC_CAND_TEXT_BYTES);
    chk("candidate blob unterminated text rejects", r_runtime_parse_candidates(blob, 8 + RTC_CAND_RECORD_BYTES, parsed, 2, &parsed_n) == -3 && parsed_n == 0);
    make_blob(blob, 1);
    blob[8 + 24 + strlen("candidate 0") + 2] = 'x';
    chk("candidate blob hidden trailing text rejects", r_runtime_parse_candidates(blob, 8 + RTC_CAND_RECORD_BYTES, parsed, 2, &parsed_n) == -3 && parsed_n == 0);
    make_blob(blob, 1);
    put32(blob + 8 + 12, 0);
    chk("candidate blob factor mismatch rejects", r_runtime_parse_candidates(blob, 8 + RTC_CAND_RECORD_BYTES, parsed, 2, &parsed_n) == -3 && parsed_n == 0);
    chk("candidate blob size helper", r_runtime_candidates_size(2) == 8 + 2 * RTC_CAND_RECORD_BYTES && r_runtime_candidates_size(-1) == 0 && r_runtime_candidates_size(RUNTIME_CHOICE_MAX_CANDIDATES + 1) == 0);
    chk("candidate blob writer rejects small buffer", r_runtime_write_candidates(blob2, sizeof blob2 - 1, perm_a, 2, &written) == -2 && written == 0);
    chk("candidate blob parser rejects small output cap", r_runtime_parse_candidates(blob2, sizeof blob2, parsed, 1, &parsed_n) == -2 && parsed_n == 0);
    chk("candidate blob writer rejects malformed", r_runtime_write_candidates(blob2, sizeof blob2, hidden_pol, 1, &written) == -3 && written == 0);
    chk("candidate blob writer round trips", r_runtime_write_candidates(blob2, sizeof blob2, perm_a, 2, &written) == 0 && written == 8 + 2 * RTC_CAND_RECORD_BYTES && r_runtime_parse_candidates(blob2, written, parsed, 2, &parsed_n) == 0 && parsed_n == 2 && parsed[0].sem_code == perm_a[0].sem_code && parsed[1].support == perm_a[1].support && !strcmp(parsed[1].text, perm_a[1].text));
    chk("candidate blob writer is little endian", blob2[0] == 'R' && blob2[1] == 'C' && blob2[2] == 'T' && blob2[3] == '1' && blob2[8] == 1 && blob2[9] == 0 && blob2[16] == 2 && blob2[17] == 0);
    memset(blob, 0xff, sizeof blob);
    chk("candidate blob writer zero pads", r_runtime_write_candidates(blob, sizeof blob, one, 1, &written) == 0 && blob[8 + 24 + strlen(one[0].text) + 1] == 0 && blob[8 + 21] == 0 && blob[8 + 22] == 0 && blob[8 + 23] == 0);
    chk("empty candidate blob size", r_runtime_candidates_size(0) == 8);
    chk("empty candidate blob writes", r_runtime_write_candidates(blob, sizeof blob, NULL, 0, &written) == 0 && written == 8);
    chk("empty candidate blob parses without output", r_runtime_parse_candidates(blob, written, NULL, 0, &parsed_n) == 0 && parsed_n == 0);
    chk("nonempty candidate blob needs output", r_runtime_parse_candidates(blob2, sizeof blob2, NULL, 2, &parsed_n) == -1 && parsed_n == 0);
    chk("code score rejects bad bits", r_runtime_code_score(0, 0, 0, &score) == -1 && r_runtime_code_score(0, 0, 65, &score) == -1 && r_runtime_code_score(0, 0, 1, NULL) == -1);
    chk("code score exact match", r_runtime_code_score(0x1234, 0x1234, 16, &score) == 0 && score == 256);
    chk("code score full mismatch one bit", r_runtime_code_score(0, 1, 1, &score) == 0 && score == -256);
    chk("code score half mismatch", r_runtime_code_score(0x0, 0x3, 4, &score) == 0 && score == 0);
    chk("code score masks high bits", r_runtime_code_score(0, 0xfffffffffffffff0ull, 4, &score) == 0 && score == 256);
    chk("code score supports 64 bits", r_runtime_code_score(0, ~0ull, 64, &score) == 0 && score == -256);
    chk("code chooser rejects bad args", r_runtime_choose_code(0, 0, code_cands, 3, &out) == -1 && out.reason == RTC_REASON_BAD_ARGUMENT && r_runtime_choose_code(0, 4, NULL, 1, &out) == -1 && out.reason == RTC_REASON_BAD_ARGUMENT);
    chk("code chooser empty abstains", r_runtime_choose_code(0, 4, NULL, 0, &out) == 0 && out.reason == RTC_REASON_NONE_NO_CANDIDATES && out.winner == -1);
    chk("code chooser picks nearest code", r_runtime_choose_code(0, 2, code_cands, 3, &out) == 0 && out.reason == RTC_REASON_OK && out.winner == 0 && out.score == 256 && out.second == 0 && out.margin == 256);
    chk("code chooser ignores sem_score", r_runtime_choose_code(1, 2, code_cands, 3, &out) == 0 && out.winner == 1 && out.score == 256);
    chk("code chooser resolves ties by lowest index", r_runtime_choose_code(2, 2, code_cands, 3, &out) == 0 && out.winner == 0 && out.score == 0 && out.margin == 0);
    chk("code chooser one candidate has no margin", r_runtime_choose_code(0, 2, code_cands, 1, &out) == 0 && out.winner == 0 && out.score == 256 && out.second == 256 && out.margin == 0);
    chk("code chooser rejects malformed candidate", r_runtime_choose_code(0, 4, hidden_pol, 1, &out) == -1 && out.reason == RTC_REASON_MALFORMED_CANDIDATE && out.winner == -1);
    runtime_candidate_t fq = {"q", 0, 0, RTC_FACTOR_POLARITY|RTC_FACTOR_COLOR|RTC_FACTOR_COMPOSITION|RTC_FACTOR_LOCATION|RTC_FACTOR_SUPPORT, RTC_POLARITY_POSITIVE, RTC_COLOR_RED, RTC_COMP_LIGHTING|RTC_COMP_ACTIVATION, 7, RTC_SUPPORT_SUPPORTED};
    runtime_candidate_t fc = {"c", 0, 0, RTC_FACTOR_POLARITY|RTC_FACTOR_COLOR|RTC_FACTOR_COMPOSITION|RTC_FACTOR_LOCATION|RTC_FACTOR_SUPPORT, RTC_POLARITY_POSITIVE, RTC_COLOR_RED, RTC_COMP_LIGHTING|RTC_COMP_ACTIVATION, 7, RTC_SUPPORT_SUPPORTED};
    chk("factor score accepts full match", r_runtime_factor_score(&fq, &fc, &score, &freason) == 0 && freason == RTC_FACTOR_REASON_OK && score == 580);
    fc.polarity = RTC_POLARITY_NEGATIVE;
    chk("factor score rejects polarity conflict", r_runtime_factor_score(&fq, &fc, &score, &freason) == 0 && freason == RTC_FACTOR_REASON_POLARITY && score == 0);
    fc.polarity = RTC_POLARITY_POSITIVE; fc.color = RTC_COLOR_BLUE;
    chk("factor score rejects color conflict", r_runtime_factor_score(&fq, &fc, &score, &freason) == 0 && freason == RTC_FACTOR_REASON_COLOR && score == 0);
    fc.color = RTC_COLOR_RED; fc.composition = RTC_COMP_LIGHTING;
    chk("factor score rejects composition miss", r_runtime_factor_score(&fq, &fc, &score, &freason) == 0 && freason == RTC_FACTOR_REASON_COMPOSITION && score == 0);
    fc.composition = RTC_COMP_LIGHTING|RTC_COMP_ACTIVATION; fc.location_id = 8;
    chk("factor score rejects location conflict", r_runtime_factor_score(&fq, &fc, &score, &freason) == 0 && freason == RTC_FACTOR_REASON_LOCATION && score == 0);
    fc.location_id = 7; fc.support = RTC_SUPPORT_UNSUPPORTED;
    chk("factor score rejects unsupported", r_runtime_factor_score(&fq, &fc, &score, &freason) == 0 && freason == RTC_FACTOR_REASON_SUPPORT && score == 0);
    runtime_candidate_t fnone_q = {"q", 0, 0, 0, 0, 0, 0, 0, 0};
    runtime_candidate_t fnone_c = {"c", 0, 0, RTC_FACTOR_SUPPORT, 0, 0, 0, 0, RTC_SUPPORT_SUPPORTED};
    chk("factor score no required factors accepts", r_runtime_factor_score(&fnone_q, &fnone_c, &score, &freason) == 0 && freason == RTC_FACTOR_REASON_OK && score == 0);
    fq.support = RTC_SUPPORT_OOD;
    fc.support = RTC_SUPPORT_SUPPORTED;
    chk("factor score rejects query OOD support", r_runtime_factor_score(&fq, &fc, &score, &freason) == 0 && freason == RTC_FACTOR_REASON_SUPPORT && score == 0);
    chk("factor score rejects malformed inputs", r_runtime_factor_score(&fq, hidden_pol, &score, &freason) == -1 && freason == RTC_FACTOR_REASON_BAD_ARGUMENT && score == 0);
    runtime_candidate_t flat_q = {"q", 0, 0, RTC_FACTOR_SUPPORT, 0, 0, 0, 0, RTC_SUPPORT_SUPPORTED};
    runtime_candidate_t flat_c[3] = {{"bad", 0, 0, RTC_FACTOR_SUPPORT, 0, 0, 0, 0, RTC_SUPPORT_UNSUPPORTED}, {"tie0", 1, 9999, RTC_FACTOR_SUPPORT, 0, 0, 0, 0, RTC_SUPPORT_SUPPORTED}, {"tie1", 1, -9999, RTC_FACTOR_SUPPORT, 0, 0, 0, 0, RTC_SUPPORT_SUPPORTED}};
    chk("flat chooser skips factor rejects", r_runtime_choose_flat(&flat_q, 2, flat_c, 3, &out, &freason) == 0 && out.reason == RTC_REASON_OK && out.winner == 1 && freason == RTC_FACTOR_REASON_OK);
    chk("flat chooser ties by lowest index", r_runtime_choose_flat(&flat_q, 2, flat_c + 1, 2, &out, &freason) == 0 && out.reason == RTC_REASON_OK && out.winner == 0 && out.margin == 0);
    chk("flat chooser ignores sem_score", r_runtime_choose_flat(&flat_q, 2, flat_c + 1, 2, &out, &freason) == 0 && out.winner == 0 && out.score == 0);
    chk("flat chooser all factor rejects abstain", r_runtime_choose_flat(&flat_q, 2, flat_c, 1, &out, &freason) == 0 && out.reason == RTC_REASON_FACTOR_REJECT && out.winner == -1 && freason == RTC_FACTOR_REASON_SUPPORT);
    chk("flat chooser empty abstains", r_runtime_choose_flat(&flat_q, 2, NULL, 0, &out, &freason) == 0 && out.reason == RTC_REASON_NONE_NO_CANDIDATES && out.winner == -1);
    chk("flat chooser rejects malformed candidate", r_runtime_choose_flat(&flat_q, 2, hidden_pol, 1, &out, &freason) == -1 && out.reason == RTC_REASON_MALFORMED_CANDIDATE && out.winner == -1);

    printf("RUNTIME_CHOICE_API checks=%d/%d\n", pass, total);
    return pass == total ? 0 : 1;
}
