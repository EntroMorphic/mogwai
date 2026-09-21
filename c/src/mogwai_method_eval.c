#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "mogwai_method.h"

typedef struct {
    int accepted_correct, wrong_action, false_action, missed_actionable, refused_negative;
} counts_t;

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

static int same_semantics(const mog_decision_t *d, const mog_case_t *c) {
    return d->intent_id == c->intent && d->object_id == c->object && d->attribute_id == c->attr;
}

static counts_t eval_counts(const router_t *r, const mog_artifact_t *a,
                            int threshold, int min_margin) {
    counts_t c = {0};
    for (size_t i = 0; i < a->nref; i++) {
        const mog_case_t *ref = &a->references[i];
        mog_decision_t d = mog_decide(r, a, ref->text, threshold, min_margin);
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
    for (size_t i = 0; i < a->nref; i++) {
        const mog_case_t *ref = &a->references[i];
        mog_decision_t d = mog_decide(r, a, ref->text, a->selected_threshold, a->selected_margin);
        printf("DECISION\t%s\t%s/%s/%s\tscore=%d\tmargin=%d\tconf=%u\trefusal=%s\ttext=%s\n",
               d.refusal_reason ? "REFUSED" : "ACCEPTED",
               intent_name(d.intent_id), object_name(d.object_id), attr_name(d.attribute_id),
               d.score, d.margin, d.confidence_bucket, refusal_name(d.refusal_reason), ref->text);
    }
}

static int run_redteam(const router_t *r, const uint8_t *wire, size_t wire_size, const mog_artifact_t *a) {
    int checks = 0, pass = 0;
    checks++;
    if (mog_artifact_valid(a)) pass++;
    uint8_t bad[MOG_WIRE_MAX_BYTES];
    mog_artifact_t tmp;
    memcpy(bad, wire, wire_size);
    ((mog_wire_header_t *)bad)->magic ^= 1u; checks++;
    if (mog_parse_wire(r, bad, wire_size, &tmp) != 0) pass++;
    checks++;
    if (mog_parse_wire(r, wire, wire_size - 1, &tmp) != 0) pass++;
    counts_t safe = eval_counts(r, a, a->selected_threshold, a->selected_margin); checks++;
    if (safe.accepted_correct == 5 && safe.wrong_action == 0 && safe.false_action == 0 &&
        safe.missed_actionable == 5 && safe.refused_negative == 2) pass++;
    counts_t loose = eval_counts(r, a, 70, 0); checks++;
    if (loose.wrong_action > 0 && loose.false_action > 0) pass++;
    mog_decision_t nonsense = mog_decide(r, a, "purple banana quantum wallpaper", a->selected_threshold, a->selected_margin); checks++;
    if (nonsense.schema_id == MOG_SCHEMA_LOG_TRIAGE && nonsense.refusal_reason != MOG_REFUSAL_NONE) pass++;
    mog_decision_t neg = mog_decide(r, a, "scheduled maintenance window", 70, 0); checks++;
    if (neg.refusal_reason == MOG_REFUSAL_NEGATIVE) pass++;
    mog_decision_t good = mog_decide(r, a, "power rail overcurrent emergency", a->selected_threshold, a->selected_margin); checks++;
    if (good.refusal_reason == MOG_REFUSAL_NONE && good.intent_id == MOG_INTENT_ALERT &&
        good.object_id == MOG_OBJ_POWER && good.attribute_id == MOG_ATTR_CRITICAL) pass++;
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
    uint8_t wire[MOG_WIRE_MAX_BYTES];
    size_t wire_size = mog_build_log_triage_wire(wire, sizeof wire);
    if (!wire_size) return 2;
    mog_artifact_t artifact;
    if (mog_parse_wire(&r, wire, wire_size, &artifact) != 0) return 3;
    if (redteam) return run_redteam(&r, wire, wire_size, &artifact);
    run_curve(&r, &artifact, details);
    return 0;
}
