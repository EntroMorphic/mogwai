#ifndef RUNTIME_CHOICE_H
#define RUNTIME_CHOICE_H

#include "router.h"
#include <stdint.h>
#include <stddef.h>

#define RUNTIME_CHOICE_MAX_CANDIDATES 32
#define RUNTIME_CHOICE_MAX_TEXT 256
#define RTC_CAND_MAGIC 0x31544352u  /* 'RTC1': runtime-choice candidate blob */
#define RTC_CAND_TEXT_BYTES (RUNTIME_CHOICE_MAX_TEXT + 1)
#define RTC_CAND_RECORD_BYTES (8u + 4u + 4u + 1u + 1u + 1u + 1u + 1u + 3u + RTC_CAND_TEXT_BYTES)
#define RTC_CAND_MAX_BYTES (8u + RUNTIME_CHOICE_MAX_CANDIDATES * RTC_CAND_RECORD_BYTES)
#define RTC_SCORE_MAX_BIT_COMPARISONS (RUNTIME_CHOICE_MAX_CANDIDATES * 64)

#define RTC_FACTOR_POLARITY    0x01u
#define RTC_FACTOR_COLOR       0x02u
#define RTC_FACTOR_COMPOSITION 0x04u
#define RTC_FACTOR_LOCATION    0x08u
#define RTC_FACTOR_SUPPORT     0x10u
#define RTC_FACTOR_KNOWN_MASK  (RTC_FACTOR_POLARITY|RTC_FACTOR_COLOR|RTC_FACTOR_COMPOSITION|RTC_FACTOR_LOCATION|RTC_FACTOR_SUPPORT)

#define RTC_POLARITY_NEGATIVE (-1)
#define RTC_POLARITY_NONE       0
#define RTC_POLARITY_POSITIVE   1

#define RTC_COLOR_NONE 0u
#define RTC_COLOR_RED  1u
#define RTC_COLOR_BLUE 2u

#define RTC_COMP_LIGHTING    0x01u
#define RTC_COMP_ACTIVATION  0x02u
#define RTC_COMP_KNOWN_MASK  (RTC_COMP_LIGHTING|RTC_COMP_ACTIVATION)

#define RTC_LOCATION_NONE 0u

#define RTC_SUPPORT_UNKNOWN     0u
#define RTC_SUPPORT_SUPPORTED   1u
#define RTC_SUPPORT_UNSUPPORTED 2u
#define RTC_SUPPORT_OOD         3u

typedef enum {
    RTC_REASON_OK = 0,
    RTC_REASON_NONE_NO_CANDIDATES,
    RTC_REASON_BAD_ARGUMENT,
    RTC_REASON_MALFORMED_QUERY,
    RTC_REASON_TOO_MANY_CANDIDATES,
    RTC_REASON_MALFORMED_CANDIDATE,
    RTC_REASON_FACTOR_REJECT,
    RTC_REASON_UNSUPPORTED_SCORER
} runtime_choice_reason_t;

typedef enum {
    RTC_FACTOR_REASON_OK = 0,
    RTC_FACTOR_REASON_BAD_ARGUMENT,
    RTC_FACTOR_REASON_POLARITY,
    RTC_FACTOR_REASON_COLOR,
    RTC_FACTOR_REASON_COMPOSITION,
    RTC_FACTOR_REASON_LOCATION,
    RTC_FACTOR_REASON_SUPPORT
} runtime_factor_reason_t;

typedef struct {
    const char *text;
    uint64_t sem_code;
    int32_t sem_score;
    /* Bitset of RTC_FACTOR_* values. Unknown bits are malformed input. */
    uint32_t factor_flags;
    int8_t polarity;
    uint8_t color;
    uint8_t composition;
    uint8_t location_id;
    uint8_t support;
} runtime_candidate_t;

#define RTC_RUNTIME_MAX_REQUEST_RECORDS (1 + RUNTIME_CHOICE_MAX_CANDIDATES)
#define RTC_RUNTIME_PERSISTED_STATE_BYTES 0u

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
int r_choose_runtime_precomputed(const runtime_candidate_t *query,
                                 int bits,
                                 const runtime_candidate_t *cands,
                                 int n_cands,
                                 runtime_choice_t *out,
                                 runtime_factor_reason_t *factor_reason_out);
int r_runtime_make_query(const char *text,
                         uint64_t sem_code,
                         int32_t sem_score,
                         uint8_t support,
                         runtime_candidate_t *out);

const char *r_runtime_reason_name(runtime_choice_reason_t reason);
int r_runtime_parse_candidates(const uint8_t *base,
                               size_t have,
                               runtime_candidate_t *out,
                               int cap,
                               int *n_out);
size_t r_runtime_candidates_size(int n_cands);
int r_runtime_write_candidates(uint8_t *dst,
                               size_t cap,
                               const runtime_candidate_t *cands,
                               int n_cands,
                               size_t *written);
int r_runtime_code_score(uint64_t query_code,
                         uint64_t candidate_code,
                         int bits,
                         int32_t *score_out);
int r_runtime_choose_code(uint64_t query_code,
                          int bits,
                          const runtime_candidate_t *cands,
                          int n_cands,
                          runtime_choice_t *out);
int r_runtime_factor_score(const runtime_candidate_t *query,
                           const runtime_candidate_t *candidate,
                           int32_t *score_out,
                           runtime_factor_reason_t *reason_out);
int r_runtime_choose_flat(const runtime_candidate_t *query,
                          int bits,
                          const runtime_candidate_t *cands,
                          int n_cands,
                          runtime_choice_t *out,
                          runtime_factor_reason_t *factor_reason_out);

#endif
