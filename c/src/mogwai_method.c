#include "mogwai_method.h"
#include <string.h>

_Static_assert(sizeof(mog_wire_header_t) == 60, "MOG1 header layout drifted");
_Static_assert(sizeof(mog_wire_case_t) == 104, "MOG1 case layout drifted");

static const mog_case_t TRAIN[] = {
    { "boot complete services nominal", MOG_INTENT_IGNORE, MOG_OBJ_SYSTEM, MOG_ATTR_INFO, 0 },
    { "heartbeat ok no faults", MOG_INTENT_IGNORE, MOG_OBJ_SYSTEM, MOG_ATTR_INFO, 0 },
    { "periodic status all sensors nominal", MOG_INTENT_IGNORE, MOG_OBJ_SENSOR, MOG_ATTR_INFO, 0 },
    { "wifi reconnecting after beacon miss", MOG_INTENT_INSPECT, MOG_OBJ_NETWORK, MOG_ATTR_WARNING, 0 },
    { "mqtt publish failures rising", MOG_INTENT_INSPECT, MOG_OBJ_NETWORK, MOG_ATTR_WARNING, 0 },
    { "packet loss above normal on uplink", MOG_INTENT_INSPECT, MOG_OBJ_NETWORK, MOG_ATTR_WARNING, 0 },
    { "temperature high reduce sample rate", MOG_INTENT_THROTTLE, MOG_OBJ_SENSOR, MOG_ATTR_WARNING, 0 },
    { "adc noise high slow acquisition", MOG_INTENT_THROTTLE, MOG_OBJ_SENSOR, MOG_ATTR_WARNING, 0 },
    { "battery low enter reduced duty", MOG_INTENT_THROTTLE, MOG_OBJ_POWER, MOG_ATTR_WARNING, 0 },
    { "watchdog timeout reboot module", MOG_INTENT_RESET, MOG_OBJ_SYSTEM, MOG_ATTR_CRITICAL, 0 },
    { "driver hung reset sensor bus", MOG_INTENT_RESET, MOG_OBJ_SENSOR, MOG_ATTR_CRITICAL, 0 },
    { "network stack wedged restart interface", MOG_INTENT_RESET, MOG_OBJ_NETWORK, MOG_ATTR_CRITICAL, 0 },
    { "flash write failed data at risk", MOG_INTENT_ALERT, MOG_OBJ_STORAGE, MOG_ATTR_CRITICAL, 0 },
    { "overcurrent detected cut power", MOG_INTENT_ALERT, MOG_OBJ_POWER, MOG_ATTR_CRITICAL, 0 },
    { "thermal shutdown imminent", MOG_INTENT_ALERT, MOG_OBJ_SYSTEM, MOG_ATTR_CRITICAL, 0 },
    { "user opened settings screen", MOG_INTENT_IGNORE, MOG_OBJ_SYSTEM, MOG_ATTR_INFO, 1 },
    { "debug trace verbose enabled", MOG_INTENT_IGNORE, MOG_OBJ_SYSTEM, MOG_ATTR_INFO, 1 },
    { "scheduled maintenance window", MOG_INTENT_IGNORE, MOG_OBJ_SYSTEM, MOG_ATTR_INFO, 1 },
};

static const mog_case_t HOLDOUT[] = {
    { "system startup finished without errors", MOG_INTENT_IGNORE, MOG_OBJ_SYSTEM, MOG_ATTR_INFO, 0 },
    { "sensor readings stable and nominal", MOG_INTENT_IGNORE, MOG_OBJ_SENSOR, MOG_ATTR_INFO, 0 },
    { "wifi association flapping", MOG_INTENT_INSPECT, MOG_OBJ_NETWORK, MOG_ATTR_WARNING, 0 },
    { "uplink latency and loss increasing", MOG_INTENT_INSPECT, MOG_OBJ_NETWORK, MOG_ATTR_WARNING, 0 },
    { "sensor temperature elevated lower polling", MOG_INTENT_THROTTLE, MOG_OBJ_SENSOR, MOG_ATTR_WARNING, 0 },
    { "battery voltage sag reduce duty cycle", MOG_INTENT_THROTTLE, MOG_OBJ_POWER, MOG_ATTR_WARNING, 0 },
    { "watchdog fired restart controller", MOG_INTENT_RESET, MOG_OBJ_SYSTEM, MOG_ATTR_CRITICAL, 0 },
    { "i2c sensor bus locked reset driver", MOG_INTENT_RESET, MOG_OBJ_SENSOR, MOG_ATTR_CRITICAL, 0 },
    { "flash crc mismatch alert operator", MOG_INTENT_ALERT, MOG_OBJ_STORAGE, MOG_ATTR_CRITICAL, 0 },
    { "power rail overcurrent emergency", MOG_INTENT_ALERT, MOG_OBJ_POWER, MOG_ATTR_CRITICAL, 0 },
    { "operator changed dashboard filter", MOG_INTENT_IGNORE, MOG_OBJ_SYSTEM, MOG_ATTR_INFO, 1 },
    { "informational log rotation complete", MOG_INTENT_IGNORE, MOG_OBJ_STORAGE, MOG_ATTR_INFO, 1 },
};

static void copy_text(char dst[MOG_WIRE_TEXT], const char *src) {
    size_t n = strlen(src);
    if (n >= MOG_WIRE_TEXT) n = MOG_WIRE_TEXT - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static int valid_nul(const char *text, size_t cap) {
    return memchr(text, 0, cap) != NULL;
}

static int valid_case_ids(const mog_wire_case_t *c) {
    return c->intent <= MOG_INTENT_ALERT && c->object <= MOG_OBJ_POWER &&
           c->attr <= MOG_ATTR_CRITICAL && c->negative <= 1;
}

static uint8_t confidence(int score, int margin, uint8_t refusal) {
    if (refusal) return MOG_CONF_REFUSE;
    if (score >= 150 && margin >= 35) return MOG_CONF_HIGH;
    if (score >= 110 && margin >= 18) return MOG_CONF_MEDIUM;
    return MOG_CONF_LOW;
}

int mog_artifact_valid(const mog_artifact_t *a) {
    return a && a->magic == MOG_ARTIFACT_MAGIC &&
           a->schema_id == MOG_SCHEMA_LOG_TRIAGE &&
           a->backend_id == MOG_BACKEND_ZERO_CENTER_TT &&
           a->score_semantics == MOG_SCORE_TT_DICE_256 &&
           a->calibration_level == MOG_CALIBRATION_HEURISTIC_BUCKET &&
           a->selected_threshold == 110 && a->selected_margin == 20 &&
           a->nex == sizeof(TRAIN)/sizeof(TRAIN[0]) &&
           a->nref == sizeof(HOLDOUT)/sizeof(HOLDOUT[0]);
}

size_t mog_build_log_triage_wire(uint8_t *buf, size_t cap) {
    size_t nex = sizeof(TRAIN) / sizeof(TRAIN[0]);
    size_t nref = sizeof(HOLDOUT) / sizeof(HOLDOUT[0]);
    size_t need = sizeof(mog_wire_header_t) + (nex + nref) * sizeof(mog_wire_case_t);
    if (cap < need || nex > MOG_LOG_TRIAGE_TRAIN_CAP || nref > MOG_LOG_TRIAGE_HOLDOUT_CAP) return 0;
    memset(buf, 0, need);
    mog_wire_header_t *h = (mog_wire_header_t *)buf;
    h->magic = MOG_ARTIFACT_MAGIC;
    h->schema_id = MOG_SCHEMA_LOG_TRIAGE;
    h->backend_id = MOG_BACKEND_ZERO_CENTER_TT;
    h->score_semantics = MOG_SCORE_TT_DICE_256;
    h->calibration_level = MOG_CALIBRATION_HEURISTIC_BUCKET;
    h->selected_threshold = 110;
    h->selected_margin = 20;
    h->nex = (uint16_t)nex;
    h->nref = (uint16_t)nref;
    strncpy(h->schema_name, "log_triage", sizeof h->schema_name - 1);
    strncpy(h->calibration_name, "heuristic_bucket", sizeof h->calibration_name - 1);
    mog_wire_case_t *w = (mog_wire_case_t *)(buf + sizeof *h);
    for (size_t i = 0; i < nex; i++, w++) {
        w->intent = TRAIN[i].intent;
        w->object = TRAIN[i].object;
        w->attr = TRAIN[i].attr;
        w->negative = (uint16_t)TRAIN[i].negative;
        copy_text(w->text, TRAIN[i].text);
    }
    for (size_t i = 0; i < nref; i++, w++) {
        w->intent = HOLDOUT[i].intent;
        w->object = HOLDOUT[i].object;
        w->attr = HOLDOUT[i].attr;
        w->negative = (uint16_t)HOLDOUT[i].negative;
        copy_text(w->text, HOLDOUT[i].text);
    }
    return need;
}

int mog_parse_wire(const router_t *r, const uint8_t *buf, size_t have, mog_artifact_t *a) {
    if (have < sizeof(mog_wire_header_t)) return -1;
    const mog_wire_header_t *h = (const mog_wire_header_t *)buf;
    if (h->magic != MOG_ARTIFACT_MAGIC || h->schema_id != MOG_SCHEMA_LOG_TRIAGE) return -2;
    if (h->backend_id != MOG_BACKEND_ZERO_CENTER_TT || h->score_semantics != MOG_SCORE_TT_DICE_256) return -3;
    if (h->calibration_level != MOG_CALIBRATION_HEURISTIC_BUCKET) return -4;
    if (h->selected_threshold != 110 || h->selected_margin != 20) return -5;
    if (h->nex == 0 || h->nex > MOG_LOG_TRIAGE_TRAIN_CAP || h->nref > MOG_LOG_TRIAGE_HOLDOUT_CAP) return -6;
    size_t need = sizeof(*h) + ((size_t)h->nex + h->nref) * sizeof(mog_wire_case_t);
    if (have != need) return -7;
    if (!valid_nul(h->schema_name, sizeof h->schema_name) ||
        !valid_nul(h->calibration_name, sizeof h->calibration_name)) return -8;
    if (strcmp(h->schema_name, "log_triage") || strcmp(h->calibration_name, "heuristic_bucket")) return -8;
    memset(a, 0, sizeof *a);
    a->magic = h->magic;
    a->schema_id = h->schema_id;
    a->backend_id = h->backend_id;
    a->score_semantics = h->score_semantics;
    a->calibration_level = h->calibration_level;
    a->selected_threshold = h->selected_threshold;
    a->selected_margin = h->selected_margin;
    a->schema_name = h->schema_name;
    a->calibration_name = h->calibration_name;
    a->nex = h->nex;
    a->nref = h->nref;
    a->wire_size = have;
    const mog_wire_case_t *w = (const mog_wire_case_t *)(buf + sizeof *h);
    for (size_t i = 0; i < a->nex; i++) {
        if (!valid_nul(w[i].text, sizeof w[i].text) || !valid_case_ids(&w[i])) return -9;
        a->exemplars[i].intent = w[i].intent;
        a->exemplars[i].object = w[i].object;
        a->exemplars[i].attr = w[i].attr;
        a->exemplars[i].negative = w[i].negative;
        a->exemplars[i].text = w[i].text;
        t_encode(r, w[i].text, &a->exemplars[i].v);
    }
    w += a->nex;
    for (size_t i = 0; i < a->nref; i++) {
        if (!valid_nul(w[i].text, sizeof w[i].text) || !valid_case_ids(&w[i])) return -10;
        a->references[i].intent = w[i].intent;
        a->references[i].object = w[i].object;
        a->references[i].attr = w[i].attr;
        a->references[i].negative = w[i].negative;
        a->references[i].text = w[i].text;
    }
    return mog_artifact_valid(a) ? 0 : -11;
}

mog_decision_t mog_decide(const router_t *r, const mog_artifact_t *a,
                          const char *text, int threshold, int min_margin) {
    tvec q;
    t_encode(r, text, &q);
    int aa = t_active(&q);
    int best = -(1 << 28), second = -(1 << 28);
    size_t bi = 0;
    for (size_t i = 0; i < a->nex; i++) {
        int s = t_score(&q, &a->exemplars[i].v, aa);
        if (s > best) { second = best; best = s; bi = i; }
        else if (s > second) second = s;
    }
    int margin = best - second;
    uint8_t refusal = MOG_REFUSAL_NONE;
    if (best < threshold) refusal = MOG_REFUSAL_BELOW_THRESHOLD;
    else if (margin < min_margin) refusal = MOG_REFUSAL_AMBIGUOUS;
    else if (a->exemplars[bi].negative) refusal = MOG_REFUSAL_NEGATIVE;
    mog_decision_t d = {
        .schema_id = MOG_SCHEMA_LOG_TRIAGE,
        .intent_id = a->exemplars[bi].intent,
        .object_id = a->exemplars[bi].object,
        .attribute_id = a->exemplars[bi].attr,
        .score = (int16_t)best,
        .margin = (int16_t)margin,
        .confidence_bucket = confidence(best, margin, refusal),
        .refusal_reason = refusal,
    };
    return d;
}
