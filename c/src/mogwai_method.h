#ifndef MOGWAI_METHOD_H
#define MOGWAI_METHOD_H

#include <stdint.h>

#define MOG_SCHEMA_LOG_TRIAGE 1u
#define MOG_ARTIFACT_MAGIC 0x4d4f4731u  /* MOG1 */
#define MOG_BACKEND_ZERO_CENTER_TT 1u
#define MOG_SCORE_TT_DICE_256 1u

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

#endif
