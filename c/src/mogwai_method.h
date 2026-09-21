#ifndef MOGWAI_METHOD_H
#define MOGWAI_METHOD_H

#include <stdint.h>
#include <stddef.h>
#include "ternary.h"

#define MOG_SCHEMA_LOG_TRIAGE 1u
#define MOG_ARTIFACT_MAGIC 0x4d4f4731u  /* MOG1 */
#define MOG_BACKEND_ZERO_CENTER_TT 1u
#define MOG_SCORE_TT_DICE_256 1u
#define MOG_LOG_TRIAGE_TRAIN_CAP 32u
#define MOG_LOG_TRIAGE_HOLDOUT_CAP 16u
#define MOG_WIRE_TEXT 96u
#define MOG_WIRE_SCHEMA 16u
#define MOG_WIRE_CALIB 24u

enum { MOG_INTENT_IGNORE = 0, MOG_INTENT_INSPECT, MOG_INTENT_THROTTLE, MOG_INTENT_RESET, MOG_INTENT_ALERT };
enum { MOG_OBJ_SYSTEM = 0, MOG_OBJ_NETWORK, MOG_OBJ_STORAGE, MOG_OBJ_SENSOR, MOG_OBJ_POWER };
enum { MOG_ATTR_INFO = 0, MOG_ATTR_WARNING, MOG_ATTR_CRITICAL };

typedef enum {
    MOG_REFUSAL_NONE = 0,
    MOG_REFUSAL_BELOW_THRESHOLD = 1,
    MOG_REFUSAL_AMBIGUOUS = 2,
    MOG_REFUSAL_NEGATIVE = 3,
    MOG_REFUSAL_UNSUPPORTED = 4,
} mog_refusal_t;

typedef enum {
    MOG_CONF_REFUSE = 0,
    MOG_CONF_LOW = 1,
    MOG_CONF_MEDIUM = 2,
    MOG_CONF_HIGH = 3,
} mog_confidence_t;

typedef enum {
    MOG_CALIBRATION_NONE = 0,
    MOG_CALIBRATION_HEURISTIC_BUCKET = 1,
    MOG_CALIBRATION_EMPIRICAL_BUCKET = 2,
    MOG_CALIBRATION_PROBABILITY_TABLE = 3,
} mog_calibration_t;

typedef struct {
    uint16_t schema_id;
    uint16_t intent_id;
    uint16_t object_id;
    uint16_t attribute_id;
    int16_t score;
    int16_t margin;
    uint8_t confidence_bucket;
    uint8_t refusal_reason;
} mog_decision_t;

typedef struct {
    const char *text;
    uint16_t intent, object, attr;
    int negative;
} mog_case_t;

typedef struct {
    uint16_t intent, object, attr;
    int negative;
    const char *text;
    tvec v;
} mog_exemplar_t;

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
    mog_exemplar_t exemplars[MOG_LOG_TRIAGE_TRAIN_CAP];
    mog_case_t references[MOG_LOG_TRIAGE_HOLDOUT_CAP];
} mog_artifact_t;

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

#define MOG_WIRE_MAX_BYTES (sizeof(mog_wire_header_t) + \
    (MOG_LOG_TRIAGE_TRAIN_CAP + MOG_LOG_TRIAGE_HOLDOUT_CAP) * sizeof(mog_wire_case_t))

int mog_artifact_valid(const mog_artifact_t *a);
size_t mog_build_log_triage_wire(uint8_t *buf, size_t cap);
int mog_parse_wire(const router_t *r, const uint8_t *buf, size_t have, mog_artifact_t *a);
mog_decision_t mog_decide(const router_t *r, const mog_artifact_t *a,
                          const char *text, int threshold, int min_margin);

#endif
