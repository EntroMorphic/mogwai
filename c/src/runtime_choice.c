#include "runtime_choice.h"
#include <string.h>

static void rtc_none(runtime_choice_t *out, runtime_choice_reason_t reason) {
    if (!out) return;
    out->winner = -1;
    out->score = 0;
    out->second = 0;
    out->margin = 0;
    out->reason = reason;
}

const char *r_runtime_reason_name(runtime_choice_reason_t reason) {
    switch (reason) {
    case RTC_REASON_OK: return "ok";
    case RTC_REASON_NONE_NO_CANDIDATES: return "none_no_candidates";
    case RTC_REASON_BAD_ARGUMENT: return "bad_argument";
    case RTC_REASON_MALFORMED_QUERY: return "malformed_query";
    case RTC_REASON_TOO_MANY_CANDIDATES: return "too_many_candidates";
    case RTC_REASON_MALFORMED_CANDIDATE: return "malformed_candidate";
    case RTC_REASON_UNSUPPORTED_SCORER: return "unsupported_scorer";
    }
    return "unknown";
}

int r_choose_runtime(const router_t *r,
                     const char *query,
                     const runtime_candidate_t *cands,
                     int n_cands,
                     runtime_choice_t *out) {
    if (!out) return -1;
    rtc_none(out, RTC_REASON_BAD_ARGUMENT);

    if (!r || !query || n_cands < 0) return -1;
    if (n_cands == 0) { rtc_none(out, RTC_REASON_NONE_NO_CANDIDATES); return 0; }
    if (!cands) return -1;
    if (n_cands > RUNTIME_CHOICE_MAX_CANDIDATES) {
        rtc_none(out, RTC_REASON_TOO_MANY_CANDIDATES);
        return -1;
    }

    size_t qlen = strnlen(query, RUNTIME_CHOICE_MAX_TEXT + 1);
    if (qlen == 0 || qlen > RUNTIME_CHOICE_MAX_TEXT) {
        rtc_none(out, RTC_REASON_MALFORMED_QUERY);
        return -1;
    }
    for (int i = 0; i < n_cands; i++) {
        if (!cands[i].text) {
            rtc_none(out, RTC_REASON_MALFORMED_CANDIDATE);
            return -1;
        }
        size_t clen = strnlen(cands[i].text, RUNTIME_CHOICE_MAX_TEXT + 1);
        if (clen == 0 || clen > RUNTIME_CHOICE_MAX_TEXT) {
            rtc_none(out, RTC_REASON_MALFORMED_CANDIDATE);
            return -1;
        }
        if (cands[i].factor_flags & ~RTC_FACTOR_KNOWN_MASK) {
            rtc_none(out, RTC_REASON_MALFORMED_CANDIDATE);
            return -1;
        }
        if (cands[i].polarity < RTC_POLARITY_NEGATIVE || cands[i].polarity > RTC_POLARITY_POSITIVE) {
            rtc_none(out, RTC_REASON_MALFORMED_CANDIDATE);
            return -1;
        }
        if (cands[i].color > RTC_COLOR_BLUE) {
            rtc_none(out, RTC_REASON_MALFORMED_CANDIDATE);
            return -1;
        }
        if (cands[i].composition & ~RTC_COMP_KNOWN_MASK) {
            rtc_none(out, RTC_REASON_MALFORMED_CANDIDATE);
            return -1;
        }
        if (cands[i].support > RTC_SUPPORT_OOD) {
            rtc_none(out, RTC_REASON_MALFORMED_CANDIDATE);
            return -1;
        }
    }

    /* P0 scaffold only: until the flat semhash+factor scorer is promoted behind
     * this API, every otherwise-valid runtime-choice call fails closed. When the
     * scorer lands, exact score ties must resolve to the lowest candidate index
     * in the caller's original order; tests pin candidate-order invariance for
     * every non-scoring outcome here. */
    rtc_none(out, RTC_REASON_UNSUPPORTED_SCORER);
    return 0;
}
