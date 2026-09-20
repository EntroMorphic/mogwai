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

    if (strnlen(query, RUNTIME_CHOICE_MAX_TEXT + 1) > RUNTIME_CHOICE_MAX_TEXT) {
        rtc_none(out, RTC_REASON_MALFORMED_CANDIDATE);
        return -1;
    }
    for (int i = 0; i < n_cands; i++) {
        if (!cands[i].text || strnlen(cands[i].text, RUNTIME_CHOICE_MAX_TEXT + 1) > RUNTIME_CHOICE_MAX_TEXT) {
            rtc_none(out, RTC_REASON_MALFORMED_CANDIDATE);
            return -1;
        }
    }

    /* P0 scaffold only: until the flat semhash+factor scorer is promoted behind
     * this API, every otherwise-valid runtime-choice call fails closed. */
    rtc_none(out, RTC_REASON_UNSUPPORTED_SCORER);
    return 0;
}
