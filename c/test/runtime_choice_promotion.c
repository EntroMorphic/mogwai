#include "../src/runtime_choice.h"
#include <stdio.h>

typedef struct {
    const char *name;
    runtime_candidate_t query;
    runtime_candidate_t cand[4];
    int n;
    int want_winner;
    runtime_choice_reason_t want_reason;
    runtime_factor_reason_t want_factor;
} promo_case_t;

static int total, pass;

static void chk(const char *name, int ok) {
    total++;
    if (ok) pass++;
    else printf("  FAIL runtime_choice_promotion: %s\n", name);
}

#define BASE(txt, code) {txt, code, 0, 0, 0, 0, 0, 0, 0}
#define SUP(txt, code) {txt, code, 0, RTC_FACTOR_SUPPORT, 0, 0, 0, 0, RTC_SUPPORT_SUPPORTED}
#define POL(txt, code, pol) {txt, code, 0, RTC_FACTOR_POLARITY|RTC_FACTOR_SUPPORT, pol, 0, 0, 0, RTC_SUPPORT_SUPPORTED}
#define COL(txt, code, color) {txt, code, 0, RTC_FACTOR_COLOR|RTC_FACTOR_SUPPORT, 0, color, 0, 0, RTC_SUPPORT_SUPPORTED}
#define LOC(txt, code, loc) {txt, code, 0, RTC_FACTOR_LOCATION|RTC_FACTOR_SUPPORT, 0, 0, 0, loc, RTC_SUPPORT_SUPPORTED}
#define COMP(txt, code, comp) {txt, code, 0, RTC_FACTOR_COMPOSITION|RTC_FACTOR_SUPPORT, 0, 0, comp, 0, RTC_SUPPORT_SUPPORTED}

int main(void) {
    promo_case_t cases[] = {
        {
            "semantic alias selects matching code",
            BASE("brew espresso", 0x15),
            {BASE("start cleaning", 0x03), BASE("make coffee", 0x15)},
            2, 1, RTC_REASON_OK, RTC_FACTOR_REASON_OK
        },
        {
            "polarity filters inverse action",
            POL("turn on plug", 0x01, RTC_POLARITY_POSITIVE),
            {POL("switch off plug", 0x01, RTC_POLARITY_NEGATIVE), POL("switch on plug", 0x03, RTC_POLARITY_POSITIVE)},
            2, 1, RTC_REASON_OK, RTC_FACTOR_REASON_OK
        },
        {
            "color conflict abstains with attribution",
            COL("set lights red", 0x02, RTC_COLOR_RED),
            {COL("set lights blue", 0x02, RTC_COLOR_BLUE)},
            1, -1, RTC_REASON_FACTOR_REJECT, RTC_FACTOR_REASON_COLOR
        },
        {
            "location conflict abstains with attribution",
            LOC("activate hallway lamps", 0x07, 1),
            {LOC("activate kitchen lamps", 0x07, 3)},
            1, -1, RTC_REASON_FACTOR_REJECT, RTC_FACTOR_REASON_LOCATION
        },
        {
            "query support rejects before semantic match",
            {"light rail getting brighter", 0x0f, 0, RTC_FACTOR_SUPPORT, 0, 0, 0, 0, RTC_SUPPORT_OOD},
            {SUP("make hallway brighter", 0x0f)},
            1, -1, RTC_REASON_FACTOR_REJECT, RTC_FACTOR_REASON_SUPPORT
        },
        {
            "composition support is required",
            COMP("activate hallway lamps", 0x21, RTC_COMP_LIGHTING|RTC_COMP_ACTIVATION),
            {COMP("hallway lamps", 0x21, RTC_COMP_LIGHTING), COMP("turn on hallway lamps", 0x20, RTC_COMP_LIGHTING|RTC_COMP_ACTIVATION)},
            2, 1, RTC_REASON_OK, RTC_FACTOR_REASON_OK
        },
        {
            "ties choose first candidate",
            SUP("clean the flat", 0x33),
            {SUP("clean apartment", 0x31), SUP("tidy the flat", 0x31)},
            2, 0, RTC_REASON_OK, RTC_FACTOR_REASON_OK
        },
        {
            "empty candidate set abstains",
            SUP("clean the flat", 0x33),
            {{0}},
            0, -1, RTC_REASON_NONE_NO_CANDIDATES, RTC_FACTOR_REASON_BAD_ARGUMENT
        }
    };

    int ncases = (int)(sizeof cases / sizeof cases[0]);
    for (int i = 0; i < ncases; i++) {
        runtime_choice_t out;
        runtime_factor_reason_t freason = RTC_FACTOR_REASON_BAD_ARGUMENT;
        int rc = r_choose_runtime_precomputed(&cases[i].query, 6, cases[i].cand, cases[i].n, &out, &freason);
        chk(cases[i].name, rc == 0 && out.winner == cases[i].want_winner && out.reason == cases[i].want_reason && freason == cases[i].want_factor);
    }

    printf("RUNTIME_CHOICE_PROMOTION checks=%d/%d\n", pass, total);
    return pass == total ? 0 : 1;
}
