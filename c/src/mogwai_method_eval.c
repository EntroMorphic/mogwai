#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "mogwai_method.h"
#include "router.h"
#include "ternary.h"

enum { INTENT_IGNORE = 0, INTENT_INSPECT, INTENT_THROTTLE, INTENT_RESET, INTENT_ALERT };
enum { OBJ_SYSTEM = 0, OBJ_NETWORK, OBJ_STORAGE, OBJ_SENSOR, OBJ_POWER };
enum { ATTR_INFO = 0, ATTR_WARNING, ATTR_CRITICAL };
enum { LOG_TRIAGE_TRAIN_CAP = 32, LOG_TRIAGE_HOLDOUT_CAP = 16 };
enum { MOG_WIRE_TEXT = 96, MOG_WIRE_SCHEMA = 16, MOG_WIRE_CALIB = 24 };

typedef struct {
    const char *text;
    uint16_t intent, object, attr;
    int negative;
} case_t;

typedef struct {
    uint16_t intent, object, attr;
    int negative;
    const char *text;
    tvec v;
} exemplar_t;

typedef struct {
    uint32_t magic;
    uint16_t schema_id;
    uint16_t backend_id;
    uint16_t score_semantics;
    uint8_t calibration_level;
    int16_t selected_threshold;
    int16_t selected_margin;
    const char *schema_name;
    const char *calibration_name;
    size_t nex, nref;
    size_t wire_size;
    exemplar_t exemplars[LOG_TRIAGE_TRAIN_CAP];
    case_t references[LOG_TRIAGE_HOLDOUT_CAP];
} mog_artifact_t;

typedef struct {
    int accepted_correct, wrong_action, false_action, missed_actionable, refused_negative;
} counts_t;

typedef struct {
    uint32_t magic;
    uint16_t schema_id;
    uint16_t backend_id;
    uint16_t score_semantics;
    uint8_t calibration_level;
    int16_t selected_threshold;
    int16_t selected_margin;
    uint16_t nex, nref;
    char schema_name[MOG_WIRE_SCHEMA];
    char calibration_name[MOG_WIRE_CALIB];
} mog_wire_header_t;

typedef struct {
    uint16_t intent, object, attr, negative;
    char text[MOG_WIRE_TEXT];
} mog_wire_case_t;

static const case_t TRAIN[] = {
    { "boot complete services nominal", INTENT_IGNORE, OBJ_SYSTEM, ATTR_INFO, 0 },
    { "heartbeat ok no faults", INTENT_IGNORE, OBJ_SYSTEM, ATTR_INFO, 0 },
    { "periodic status all sensors nominal", INTENT_IGNORE, OBJ_SENSOR, ATTR_INFO, 0 },
    { "wifi reconnecting after beacon miss", INTENT_INSPECT, OBJ_NETWORK, ATTR_WARNING, 0 },
    { "mqtt publish failures rising", INTENT_INSPECT, OBJ_NETWORK, ATTR_WARNING, 0 },
    { "packet loss above normal on uplink", INTENT_INSPECT, OBJ_NETWORK, ATTR_WARNING, 0 },
    { "temperature high reduce sample rate", INTENT_THROTTLE, OBJ_SENSOR, ATTR_WARNING, 0 },
    { "adc noise high slow acquisition", INTENT_THROTTLE, OBJ_SENSOR, ATTR_WARNING, 0 },
    { "battery low enter reduced duty", INTENT_THROTTLE, OBJ_POWER, ATTR_WARNING, 0 },
    { "watchdog timeout reboot module", INTENT_RESET, OBJ_SYSTEM, ATTR_CRITICAL, 0 },
    { "driver hung reset sensor bus", INTENT_RESET, OBJ_SENSOR, ATTR_CRITICAL, 0 },
    { "network stack wedged restart interface", INTENT_RESET, OBJ_NETWORK, ATTR_CRITICAL, 0 },
    { "flash write failed data at risk", INTENT_ALERT, OBJ_STORAGE, ATTR_CRITICAL, 0 },
    { "overcurrent detected cut power", INTENT_ALERT, OBJ_POWER, ATTR_CRITICAL, 0 },
    { "thermal shutdown imminent", INTENT_ALERT, OBJ_SYSTEM, ATTR_CRITICAL, 0 },
    { "user opened settings screen", INTENT_IGNORE, OBJ_SYSTEM, ATTR_INFO, 1 },
    { "debug trace verbose enabled", INTENT_IGNORE, OBJ_SYSTEM, ATTR_INFO, 1 },
    { "scheduled maintenance window", INTENT_IGNORE, OBJ_SYSTEM, ATTR_INFO, 1 },
};

static const case_t HOLDOUT[] = {
    { "system startup finished without errors", INTENT_IGNORE, OBJ_SYSTEM, ATTR_INFO, 0 },
    { "sensor readings stable and nominal", INTENT_IGNORE, OBJ_SENSOR, ATTR_INFO, 0 },
    { "wifi association flapping", INTENT_INSPECT, OBJ_NETWORK, ATTR_WARNING, 0 },
    { "uplink latency and loss increasing", INTENT_INSPECT, OBJ_NETWORK, ATTR_WARNING, 0 },
    { "sensor temperature elevated lower polling", INTENT_THROTTLE, OBJ_SENSOR, ATTR_WARNING, 0 },
    { "battery voltage sag reduce duty cycle", INTENT_THROTTLE, OBJ_POWER, ATTR_WARNING, 0 },
    { "watchdog fired restart controller", INTENT_RESET, OBJ_SYSTEM, ATTR_CRITICAL, 0 },
    { "i2c sensor bus locked reset driver", INTENT_RESET, OBJ_SENSOR, ATTR_CRITICAL, 0 },
    { "flash crc mismatch alert operator", INTENT_ALERT, OBJ_STORAGE, ATTR_CRITICAL, 0 },
    { "power rail overcurrent emergency", INTENT_ALERT, OBJ_POWER, ATTR_CRITICAL, 0 },
    { "operator changed dashboard filter", INTENT_IGNORE, OBJ_SYSTEM, ATTR_INFO, 1 },
    { "informational log rotation complete", INTENT_IGNORE, OBJ_STORAGE, ATTR_INFO, 1 },
};

static const char *intent_name(uint16_t x) {
    static const char *N[] = { "ignore", "inspect", "throttle", "reset", "alert" };
    return x < sizeof(N)/sizeof(N[0]) ? N[x] : "?";
}

static const char *object_name(uint16_t x) {
    static const char *N[] = { "system", "network", "storage", "sensor", "power" };
    return x < sizeof(N)/sizeof(N[0]) ? N[x] : "?";
}

static const char *attr_name(uint16_t x) {
    static const char *N[] = { "info", "warning", "critical" };
    return x < sizeof(N)/sizeof(N[0]) ? N[x] : "?";
}

static const char *refusal_name(uint8_t x) {
    switch (x) {
    case MOG_REFUSAL_NONE: return "none";
    case MOG_REFUSAL_BELOW_THRESHOLD: return "below_threshold";
    case MOG_REFUSAL_AMBIGUOUS: return "ambiguous";
    case MOG_REFUSAL_NEGATIVE: return "negative";
    default: return "unsupported";
    }
}

static uint8_t confidence(int score, int margin, uint8_t refusal) {
    if (refusal) return MOG_CONF_REFUSE;
    if (score >= 150 && margin >= 35) return MOG_CONF_HIGH;
    if (score >= 110 && margin >= 18) return MOG_CONF_MEDIUM;
    return MOG_CONF_LOW;
}

static int artifact_valid(const mog_artifact_t *a) {
    return a && a->magic == MOG_ARTIFACT_MAGIC &&
           a->schema_id == MOG_SCHEMA_LOG_TRIAGE &&
           a->backend_id == MOG_BACKEND_ZERO_CENTER_TT &&
           a->score_semantics == MOG_SCORE_TT_DICE_256 &&
           a->calibration_level == MOG_CALIBRATION_HEURISTIC_BUCKET &&
           a->selected_threshold == 110 && a->selected_margin == 20 &&
           a->nex == sizeof(TRAIN)/sizeof(TRAIN[0]) &&
           a->nref == sizeof(HOLDOUT)/sizeof(HOLDOUT[0]);
}

static void copy_text(char dst[MOG_WIRE_TEXT], const char *src) {
    size_t n = strlen(src);
    if (n >= MOG_WIRE_TEXT) n = MOG_WIRE_TEXT - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static size_t serialize_wire(uint8_t *buf, size_t cap) {
    size_t nex = sizeof(TRAIN) / sizeof(TRAIN[0]);
    size_t nref = sizeof(HOLDOUT) / sizeof(HOLDOUT[0]);
    size_t need = sizeof(mog_wire_header_t) + (nex + nref) * sizeof(mog_wire_case_t);
    if (cap < need || nex > LOG_TRIAGE_TRAIN_CAP || nref > LOG_TRIAGE_HOLDOUT_CAP) return 0;
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

static int valid_nul(const char *text, size_t cap) {
    return memchr(text, 0, cap) != NULL;
}

static int parse_wire(const router_t *r, const uint8_t *buf, size_t have, mog_artifact_t *a) {
    if (have < sizeof(mog_wire_header_t)) return -1;
    const mog_wire_header_t *h = (const mog_wire_header_t *)buf;
    if (h->magic != MOG_ARTIFACT_MAGIC || h->schema_id != MOG_SCHEMA_LOG_TRIAGE) return -2;
    if (h->backend_id != MOG_BACKEND_ZERO_CENTER_TT || h->score_semantics != MOG_SCORE_TT_DICE_256) return -3;
    if (h->calibration_level != MOG_CALIBRATION_HEURISTIC_BUCKET) return -4;
    if (h->selected_threshold != 110 || h->selected_margin != 20) return -5;
    if (h->nex == 0 || h->nex > LOG_TRIAGE_TRAIN_CAP || h->nref > LOG_TRIAGE_HOLDOUT_CAP) return -6;
    size_t need = sizeof(*h) + ((size_t)h->nex + h->nref) * sizeof(mog_wire_case_t);
    if (have != need) return -7;
    if (!valid_nul(h->schema_name, sizeof h->schema_name) ||
        !valid_nul(h->calibration_name, sizeof h->calibration_name)) return -8;
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
        if (!valid_nul(w[i].text, sizeof w[i].text) || w[i].negative > 1) return -9;
        a->exemplars[i].intent = w[i].intent;
        a->exemplars[i].object = w[i].object;
        a->exemplars[i].attr = w[i].attr;
        a->exemplars[i].negative = w[i].negative;
        a->exemplars[i].text = w[i].text;
        t_encode(r, w[i].text, &a->exemplars[i].v);
    }
    w += a->nex;
    for (size_t i = 0; i < a->nref; i++) {
        if (!valid_nul(w[i].text, sizeof w[i].text) || w[i].negative > 1) return -10;
        a->references[i].intent = w[i].intent;
        a->references[i].object = w[i].object;
        a->references[i].attr = w[i].attr;
        a->references[i].negative = w[i].negative;
        a->references[i].text = w[i].text;
    }
    return artifact_valid(a) ? 0 : -11;
}

static mog_decision_t decide(const router_t *r, const mog_artifact_t *a,
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

static int same_semantics(const mog_decision_t *d, const case_t *c) {
    return d->intent_id == c->intent && d->object_id == c->object && d->attribute_id == c->attr;
}

static counts_t eval_counts(const router_t *r, const mog_artifact_t *a,
                            int threshold, int min_margin) {
    counts_t c = {0};
    for (size_t i = 0; i < a->nref; i++) {
        const case_t *ref = &a->references[i];
        mog_decision_t d = decide(r, a, ref->text, threshold, min_margin);
        int refused = d.refusal_reason != MOG_REFUSAL_NONE;
        if (refused && ref->negative) c.refused_negative++;
        else if (refused) c.missed_actionable++;
        else if (ref->negative) c.false_action++;
        else if (same_semantics(&d, ref)) c.accepted_correct++;
        else c.wrong_action++;
    }
    return c;
}

static void run_curve(const router_t *r, const mog_artifact_t *a, int details) {
    printf("MOGWAI_METHOD_LOG_TRIAGE schema=%u train=%lu holdout=%lu\n",
           a->schema_id, (unsigned long)a->nex, (unsigned long)a->nref);
    printf("ABI fields=schema_id,intent_id,object_id,attribute_id,score,margin,confidence_bucket,refusal_reason\n");
    printf("ARTIFACT magic=MOG1 wire_bytes=%lu schema=%s backend=zero_center_twin_ternary score=tt_dice_256 calibration=%s threshold=%d margin=%d refs=%lu\n",
           (unsigned long)a->wire_size,
           a->schema_name, a->calibration_name, a->selected_threshold,
           a->selected_margin, (unsigned long)a->nref);
    printf("CURVE\tthreshold\tmargin\taccepted_correct\twrong_action\tfalse_action\tmissed_actionable\trefused_negative\n");
    const int thresholds[] = { 70, 90, 110, 130 };
    const int margins[] = { 0, 10, 20, 30 };
    for (size_t ti = 0; ti < sizeof(thresholds)/sizeof(thresholds[0]); ti++) {
        for (size_t mi = 0; mi < sizeof(margins)/sizeof(margins[0]); mi++) {
            counts_t c = eval_counts(r, a, thresholds[ti], margins[mi]);
            printf("CURVE\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", thresholds[ti], margins[mi],
                   c.accepted_correct, c.wrong_action, c.false_action, c.missed_actionable, c.refused_negative);
        }
    }
    if (!details) return;
    printf("\nDETAILS threshold=110 margin=20\n");
    for (size_t i = 0; i < sizeof(HOLDOUT)/sizeof(HOLDOUT[0]); i++) {
        const case_t *ref = &a->references[i];
        mog_decision_t d = decide(r, a, ref->text, a->selected_threshold, a->selected_margin);
        printf("DECISION\t%s\t%s/%s/%s\tscore=%d\tmargin=%d\tconf=%u\trefusal=%s\ttext=%s\n",
               d.refusal_reason ? "REFUSED" : "ACCEPTED",
               intent_name(d.intent_id), object_name(d.object_id), attr_name(d.attribute_id),
               d.score, d.margin, d.confidence_bucket, refusal_name(d.refusal_reason), ref->text);
    }
}

static int run_redteam(const router_t *r, const uint8_t *wire, size_t wire_size, const mog_artifact_t *a) {
    int checks = 0, pass = 0;
    checks++;
    if (artifact_valid(a)) pass++;
    uint8_t bad[sizeof(mog_wire_header_t) +
                (LOG_TRIAGE_TRAIN_CAP + LOG_TRIAGE_HOLDOUT_CAP) * sizeof(mog_wire_case_t)];
    mog_artifact_t tmp;
    memcpy(bad, wire, wire_size);
    ((mog_wire_header_t *)bad)->magic ^= 1u; checks++;
    if (parse_wire(r, bad, wire_size, &tmp) != 0) pass++;
    checks++;
    if (parse_wire(r, wire, wire_size - 1, &tmp) != 0) pass++;
    counts_t safe = eval_counts(r, a, a->selected_threshold, a->selected_margin); checks++;
    if (safe.accepted_correct == 5 && safe.wrong_action == 0 && safe.false_action == 0 &&
        safe.missed_actionable == 5 && safe.refused_negative == 2) pass++;
    counts_t loose = eval_counts(r, a, 70, 0); checks++;
    if (loose.wrong_action > 0 && loose.false_action > 0) pass++;
    mog_decision_t nonsense = decide(r, a, "purple banana quantum wallpaper", a->selected_threshold, a->selected_margin); checks++;
    if (nonsense.schema_id == MOG_SCHEMA_LOG_TRIAGE && nonsense.refusal_reason != MOG_REFUSAL_NONE) pass++;
    mog_decision_t neg = decide(r, a, "scheduled maintenance window", 70, 0); checks++;
    if (neg.refusal_reason == MOG_REFUSAL_NEGATIVE) pass++;
    mog_decision_t good = decide(r, a, "power rail overcurrent emergency", a->selected_threshold, a->selected_margin); checks++;
    if (good.refusal_reason == MOG_REFUSAL_NONE && good.intent_id == INTENT_ALERT &&
        good.object_id == OBJ_POWER && good.attribute_id == ATTR_CRITICAL) pass++;
    printf("MOGWAI_METHOD_REDTEAM checks=%d/%d\n", pass, checks);
    return pass == checks ? 0 : 1;
}

int main(int argc, char **argv) {
    int details = 0, redteam = 0;
    if (argc > 2) {
        fprintf(stderr, "usage: %s [--details|--redteam]\n", argv[0]);
        return 1;
    }
    if (argc == 2) {
        if (!strcmp(argv[1], "--details")) details = 1;
        else if (!strcmp(argv[1], "--redteam")) redteam = 1;
        else {
            fprintf(stderr, "usage: %s [--details|--redteam]\n", argv[0]);
            return 1;
        }
    }
    router_t r = {0};
    r.dim = RD;
    uint8_t wire[sizeof(mog_wire_header_t) +
                 (LOG_TRIAGE_TRAIN_CAP + LOG_TRIAGE_HOLDOUT_CAP) * sizeof(mog_wire_case_t)];
    size_t wire_size = serialize_wire(wire, sizeof wire);
    if (!wire_size) return 2;
    mog_artifact_t artifact;
    if (parse_wire(&r, wire, wire_size, &artifact) != 0) return 3;
    if (redteam) return run_redteam(&r, wire, wire_size, &artifact);
    run_curve(&r, &artifact, details);
    return 0;
}
