#ifndef RUNTIME_CHOICE_H
#define RUNTIME_CHOICE_H

#include "router.h"
#include <stdint.h>

#define RUNTIME_CHOICE_MAX_CANDIDATES 32
#define RUNTIME_CHOICE_MAX_TEXT 256

#define RTC_FACTOR_POLARITY    0x01u
#define RTC_FACTOR_COLOR       0x02u
#define RTC_FACTOR_COMPOSITION 0x04u
#define RTC_FACTOR_LOCATION    0x08u
#define RTC_FACTOR_SUPPORT     0x10u
#define RTC_FACTOR_KNOWN_MASK  (RTC_FACTOR_POLARITY|RTC_FACTOR_COLOR|RTC_FACTOR_COMPOSITION|RTC_FACTOR_LOCATION|RTC_FACTOR_SUPPORT)

typedef enum {
    RTC_REASON_OK = 0,
    RTC_REASON_NONE_NO_CANDIDATES,
    RTC_REASON_BAD_ARGUMENT,
    RTC_REASON_TOO_MANY_CANDIDATES,
    RTC_REASON_MALFORMED_CANDIDATE,
    RTC_REASON_UNSUPPORTED_SCORER
} runtime_choice_reason_t;

typedef struct {
    const char *text;
    uint64_t sem_code;
    int32_t sem_score;
    /* Bitset of RTC_FACTOR_* values. Unknown bits are malformed input. */
    uint32_t factor_flags;
} runtime_candidate_t;

typedef struct {
    int winner;
    int32_t score;
    int32_t second;
    int32_t margin;
    runtime_choice_reason_t reason;
} runtime_choice_t;

int r_choose_runtime(const router_t *r,
                     const char *query,
                     const runtime_candidate_t *cands,
                     int n_cands,
                     runtime_choice_t *out);

#endif
