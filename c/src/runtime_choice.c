#include "runtime_choice.h"
#include <string.h>

static int rtc_validate_candidate(const runtime_candidate_t *c);

static int rtc_word_at(const char *h, const char *w) {
    size_t n = strlen(w);
    for (const char *p = strstr(h, w); p; p = strstr(p + 1, w))
        if (p > h && p[-1] == ' ' && (p[n] == ' ' || p[n] == 0)) return (int)(p - h);
    return -1;
}

static int rtc_has_word(const char *text, const char *word) {
    char b[512];
    r_norm(text, b, sizeof b);
    return rtc_word_at(b, word) >= 0;
}

static int rtc_has_phrase(const char *text, const char *phrase) {
    char b[512], p[128];
    r_norm(text, b, sizeof b);
    r_norm(phrase, p, sizeof p);
    return strstr(b, p) != NULL;
}

static void rtc_none(runtime_choice_t *out, runtime_choice_reason_t reason) {
    if (!out) return;
    out->winner = -1;
    out->score = 0;
    out->second = 0;
    out->margin = 0;
    out->reason = reason;
}

static int rtc_text_polarity(const char *text) {
    int pos = rtc_has_word(text, "on") || rtc_has_word(text, "activate") || rtc_has_word(text, "brighter") || rtc_has_word(text, "brighten") || rtc_has_word(text, "increase") || rtc_has_word(text, "raise");
    int neg = rtc_has_word(text, "off") || rtc_has_word(text, "dim") || rtc_has_word(text, "darker") || rtc_has_word(text, "lower") || rtc_has_word(text, "decrease") || rtc_has_word(text, "reduce");
    if (pos && !neg) return RTC_POLARITY_POSITIVE;
    if (neg && !pos) return RTC_POLARITY_NEGATIVE;
    return RTC_POLARITY_NONE;
}

static uint8_t rtc_text_color(const char *text) {
    if (rtc_has_word(text, "red") || rtc_has_word(text, "crimson")) return RTC_COLOR_RED;
    if (rtc_has_word(text, "blue")) return RTC_COLOR_BLUE;
    return RTC_COLOR_NONE;
}

static uint8_t rtc_text_composition(const char *text) {
    uint8_t c = 0;
    if (rtc_has_word(text, "light") || rtc_has_word(text, "lights") || rtc_has_word(text, "lamp") || rtc_has_word(text, "lamps") || rtc_has_word(text, "lighting")) c |= RTC_COMP_LIGHTING;
    if (rtc_has_word(text, "activate") || rtc_has_word(text, "on")) c |= RTC_COMP_ACTIVATION;
    return c;
}

static uint8_t rtc_text_location(const char *text) {
    static const char *names[] = {"hallway", "bedroom", "kitchen", "lounge", "atrium", "workshop", "nursery", "observatory", "living room", "dining room"};
    for (uint8_t i = 0; i < (uint8_t)(sizeof names / sizeof names[0]); i++) if (rtc_has_phrase(text, names[i])) return (uint8_t)(i + 1);
    return RTC_LOCATION_NONE;
}

int r_runtime_make_query(const char *text,
                         uint64_t sem_code,
                         int32_t sem_score,
                         uint8_t support,
                         runtime_candidate_t *out) {
    if (!text || !out) return -1;
    size_t len = strnlen(text, RUNTIME_CHOICE_MAX_TEXT + 1);
    if (len == 0 || len > RUNTIME_CHOICE_MAX_TEXT) return -1;
    if (support > RTC_SUPPORT_OOD) return -1;
    memset(out, 0, sizeof *out);
    out->text = text;
    out->sem_code = sem_code;
    out->sem_score = sem_score;
    out->support = support;
    if (support != RTC_SUPPORT_UNKNOWN) out->factor_flags |= RTC_FACTOR_SUPPORT;
    out->polarity = (int8_t)rtc_text_polarity(text);
    if (out->polarity != RTC_POLARITY_NONE) out->factor_flags |= RTC_FACTOR_POLARITY;
    out->color = rtc_text_color(text);
    if (out->color != RTC_COLOR_NONE) out->factor_flags |= RTC_FACTOR_COLOR;
    out->composition = rtc_text_composition(text);
    if (out->composition) out->factor_flags |= RTC_FACTOR_COMPOSITION;
    out->location_id = rtc_text_location(text);
    if (out->location_id != RTC_LOCATION_NONE) out->factor_flags |= RTC_FACTOR_LOCATION;
    return rtc_validate_candidate(out) ? 0 : -1;
}

const char *r_runtime_reason_name(runtime_choice_reason_t reason) {
    switch (reason) {
    case RTC_REASON_OK: return "ok";
    case RTC_REASON_NONE_NO_CANDIDATES: return "none_no_candidates";
    case RTC_REASON_BAD_ARGUMENT: return "bad_argument";
    case RTC_REASON_MALFORMED_QUERY: return "malformed_query";
    case RTC_REASON_TOO_MANY_CANDIDATES: return "too_many_candidates";
    case RTC_REASON_MALFORMED_CANDIDATE: return "malformed_candidate";
    case RTC_REASON_FACTOR_REJECT: return "factor_reject";
    case RTC_REASON_UNSUPPORTED_SCORER: return "unsupported_scorer";
    }
    return "unknown";
}

static int rtc_validate_candidate(const runtime_candidate_t *c) {
    if (!c || !c->text) return 0;
    size_t clen = strnlen(c->text, RUNTIME_CHOICE_MAX_TEXT + 1);
    if (clen == 0 || clen > RUNTIME_CHOICE_MAX_TEXT) return 0;
    if (c->factor_flags & ~RTC_FACTOR_KNOWN_MASK) return 0;
    if (c->polarity < RTC_POLARITY_NEGATIVE || c->polarity > RTC_POLARITY_POSITIVE) return 0;
    if (c->color > RTC_COLOR_BLUE) return 0;
    if (c->composition & ~RTC_COMP_KNOWN_MASK) return 0;
    if (c->support > RTC_SUPPORT_OOD) return 0;
    if (((c->factor_flags & RTC_FACTOR_POLARITY) != 0) != (c->polarity != RTC_POLARITY_NONE)) return 0;
    if (((c->factor_flags & RTC_FACTOR_COLOR) != 0) != (c->color != RTC_COLOR_NONE)) return 0;
    if (((c->factor_flags & RTC_FACTOR_COMPOSITION) != 0) != (c->composition != 0)) return 0;
    if (((c->factor_flags & RTC_FACTOR_LOCATION) != 0) != (c->location_id != RTC_LOCATION_NONE)) return 0;
    if (((c->factor_flags & RTC_FACTOR_SUPPORT) != 0) != (c->support != RTC_SUPPORT_UNKNOWN)) return 0;
    return 1;
}

static uint32_t rtc_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rtc_u64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}
static int32_t rtc_i32(const uint8_t *p) { return (int32_t)rtc_u32(p); }
static void rtc_put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void rtc_put64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) { p[i] = (uint8_t)v; v >>= 8; }
}
static void rtc_puti32(uint8_t *p, int32_t v) { rtc_put32(p, (uint32_t)v); }

int r_runtime_code_score(uint64_t query_code,
                         uint64_t candidate_code,
                         int bits,
                         int32_t *score_out) {
    if (!score_out || bits < 1 || bits > 64) return -1;
    uint64_t mask = bits == 64 ? ~0ull : ((1ull << bits) - 1ull);
    int dist = __builtin_popcountll((query_code ^ candidate_code) & mask);
    *score_out = ((bits - 2 * dist) * 256) / bits;
    return 0;
}

int r_runtime_choose_code(uint64_t query_code,
                          int bits,
                          const runtime_candidate_t *cands,
                          int n_cands,
                          runtime_choice_t *out) {
    if (!out) return -1;
    rtc_none(out, RTC_REASON_BAD_ARGUMENT);
    if (bits < 1 || bits > 64 || n_cands < 0) return -1;
    if (n_cands == 0) { rtc_none(out, RTC_REASON_NONE_NO_CANDIDATES); return 0; }
    if (!cands) return -1;
    if (n_cands > RUNTIME_CHOICE_MAX_CANDIDATES) { rtc_none(out, RTC_REASON_TOO_MANY_CANDIDATES); return -1; }

    int winner = -1;
    int32_t best = -257, second = -257;
    for (int i = 0; i < n_cands; i++) {
        if (!rtc_validate_candidate(&cands[i])) { rtc_none(out, RTC_REASON_MALFORMED_CANDIDATE); return -1; }
        int32_t s;
        if (r_runtime_code_score(query_code, cands[i].sem_code, bits, &s) != 0) return -1;
        if (s > best) { second = best; best = s; winner = i; }
        else if (s > second) second = s;
    }
    out->winner = winner;
    out->score = best;
    out->second = n_cands == 1 ? best : second;
    out->margin = n_cands == 1 ? 0 : best - second;
    out->reason = RTC_REASON_OK;
    return 0;
}

int r_runtime_factor_score(const runtime_candidate_t *query,
                           const runtime_candidate_t *candidate,
                           int32_t *score_out,
                           runtime_factor_reason_t *reason_out) {
    if (score_out) *score_out = 0;
    if (reason_out) *reason_out = RTC_FACTOR_REASON_BAD_ARGUMENT;
    if (!query || !candidate || !score_out || !reason_out) return -1;
    if (!rtc_validate_candidate(query) || !rtc_validate_candidate(candidate)) return -1;

    int32_t score = 0;
    if (query->factor_flags & RTC_FACTOR_SUPPORT) {
        if (!(candidate->factor_flags & RTC_FACTOR_SUPPORT) ||
            query->support != RTC_SUPPORT_SUPPORTED ||
            candidate->support != RTC_SUPPORT_SUPPORTED) {
            *reason_out = RTC_FACTOR_REASON_SUPPORT;
            return 0;
        }
    }
    if (query->factor_flags & RTC_FACTOR_POLARITY) {
        if (!(candidate->factor_flags & RTC_FACTOR_POLARITY) || query->polarity != candidate->polarity) {
            *reason_out = RTC_FACTOR_REASON_POLARITY;
            return 0;
        }
        score += 80;
    }
    if (query->factor_flags & RTC_FACTOR_COLOR) {
        if (!(candidate->factor_flags & RTC_FACTOR_COLOR) || query->color != candidate->color) {
            *reason_out = RTC_FACTOR_REASON_COLOR;
            return 0;
        }
        score += 180;
    }
    if (query->factor_flags & RTC_FACTOR_COMPOSITION) {
        if ((candidate->composition & query->composition) != query->composition) {
            *reason_out = RTC_FACTOR_REASON_COMPOSITION;
            return 0;
        }
        score += 320;
    }
    if (query->factor_flags & RTC_FACTOR_LOCATION) {
        if (!(candidate->factor_flags & RTC_FACTOR_LOCATION) || query->location_id != candidate->location_id) {
            *reason_out = RTC_FACTOR_REASON_LOCATION;
            return 0;
        }
    }

    *score_out = score;
    *reason_out = RTC_FACTOR_REASON_OK;
    return 0;
}

int r_runtime_choose_flat(const runtime_candidate_t *query,
                          int bits,
                          const runtime_candidate_t *cands,
                          int n_cands,
                          runtime_choice_t *out,
                          runtime_factor_reason_t *factor_reason_out) {
    if (factor_reason_out) *factor_reason_out = RTC_FACTOR_REASON_BAD_ARGUMENT;
    if (!out) return -1;
    rtc_none(out, RTC_REASON_BAD_ARGUMENT);
    if (!query || bits < 1 || bits > 64 || n_cands < 0) return -1;
    if (!rtc_validate_candidate(query)) return -1;
    if (n_cands == 0) { rtc_none(out, RTC_REASON_NONE_NO_CANDIDATES); return 0; }
    if (!cands) return -1;
    if (n_cands > RUNTIME_CHOICE_MAX_CANDIDATES) { rtc_none(out, RTC_REASON_TOO_MANY_CANDIDATES); return -1; }

    int winner = -1, accepted = 0;
    int32_t best = -1000000, second = -1000000;
    runtime_factor_reason_t first_reject = RTC_FACTOR_REASON_OK;
    for (int i = 0; i < n_cands; i++) {
        if (!rtc_validate_candidate(&cands[i])) { rtc_none(out, RTC_REASON_MALFORMED_CANDIDATE); return -1; }
        int32_t factor_score, code_score;
        runtime_factor_reason_t freason;
        if (r_runtime_factor_score(query, &cands[i], &factor_score, &freason) != 0) return -1;
        if (freason != RTC_FACTOR_REASON_OK) {
            if (first_reject == RTC_FACTOR_REASON_OK) first_reject = freason;
            continue;
        }
        if (r_runtime_code_score(query->sem_code, cands[i].sem_code, bits, &code_score) != 0) return -1;
        int32_t total = code_score + factor_score;
        accepted++;
        if (total > best) { second = best; best = total; winner = i; }
        else if (total > second) second = total;
    }

    if (winner < 0) {
        rtc_none(out, RTC_REASON_FACTOR_REJECT);
        if (factor_reason_out) *factor_reason_out = first_reject;
        return 0;
    }
    out->winner = winner;
    out->score = best;
    out->second = accepted == 1 ? best : second;
    out->margin = accepted == 1 ? 0 : best - second;
    out->reason = RTC_REASON_OK;
    if (factor_reason_out) *factor_reason_out = RTC_FACTOR_REASON_OK;
    return 0;
}

size_t r_runtime_candidates_size(int n_cands) {
    if (n_cands < 0 || n_cands > RUNTIME_CHOICE_MAX_CANDIDATES) return 0;
    return 8 + (size_t)n_cands * RTC_CAND_RECORD_BYTES;
}

int r_runtime_write_candidates(uint8_t *dst,
                               size_t cap,
                               const runtime_candidate_t *cands,
                               int n_cands,
                               size_t *written) {
    if (written) *written = 0;
    if (!dst || !written) return -1;
    size_t need = r_runtime_candidates_size(n_cands);
    if (!need || need > cap) return -2;
    if (n_cands > 0 && !cands) return -1;
    for (int i = 0; i < n_cands; i++) if (!rtc_validate_candidate(&cands[i])) return -3;

    memset(dst, 0, need);
    rtc_put32(dst, RTC_CAND_MAGIC);
    rtc_put32(dst + 4, (uint32_t)n_cands);
    uint8_t *p = dst + 8;
    for (int i = 0; i < n_cands; i++, p += RTC_CAND_RECORD_BYTES) {
        rtc_put64(p, cands[i].sem_code);
        rtc_puti32(p + 8, cands[i].sem_score);
        rtc_put32(p + 12, cands[i].factor_flags);
        p[16] = (uint8_t)cands[i].polarity;
        p[17] = cands[i].color;
        p[18] = cands[i].composition;
        p[19] = cands[i].location_id;
        p[20] = cands[i].support;
        memcpy(p + 24, cands[i].text, strlen(cands[i].text) + 1);
    }
    *written = need;
    return 0;
}

int r_runtime_parse_candidates(const uint8_t *base,
                               size_t have,
                               runtime_candidate_t *out,
                               int cap,
                               int *n_out) {
    if (n_out) *n_out = 0;
    if (!base || !n_out || cap < 0) return -1;
    if (have < 8) return -2;
    if (rtc_u32(base) != RTC_CAND_MAGIC) return -2;
    uint32_t n = rtc_u32(base + 4);
    if (n > RUNTIME_CHOICE_MAX_CANDIDATES || n > (uint32_t)cap) return -2;
    if (n > 0 && !out) return -1;
    size_t need = 8 + (size_t)n * RTC_CAND_RECORD_BYTES;
    if (need != have) return -2;

    const uint8_t *p = base + 8;
    for (uint32_t i = 0; i < n; i++, p += RTC_CAND_RECORD_BYTES) {
        runtime_candidate_t c;
        c.sem_code = rtc_u64(p);
        c.sem_score = rtc_i32(p + 8);
        c.factor_flags = rtc_u32(p + 12);
        c.polarity = (int8_t)p[16];
        c.color = p[17];
        c.composition = p[18];
        c.location_id = p[19];
        c.support = p[20];
        if (p[21] || p[22] || p[23]) return -3;
        c.text = (const char *)(const void *)(p + 24);
        size_t len = strnlen(c.text, RTC_CAND_TEXT_BYTES);
        if (len == RTC_CAND_TEXT_BYTES) return -3;
        for (size_t k = len + 1; k < RTC_CAND_TEXT_BYTES; k++) if (p[24 + k]) return -3;
        if (!rtc_validate_candidate(&c)) return -3;
        out[i] = c;
    }
    *n_out = (int)n;
    return 0;
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
        if (!rtc_validate_candidate(&cands[i])) {
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

int r_choose_runtime_precomputed(const runtime_candidate_t *query,
                                 int bits,
                                 const runtime_candidate_t *cands,
                                 int n_cands,
                                 runtime_choice_t *out,
                                 runtime_factor_reason_t *factor_reason_out) {
    return r_runtime_choose_flat(query, bits, cands, n_cands, out, factor_reason_out);
}
