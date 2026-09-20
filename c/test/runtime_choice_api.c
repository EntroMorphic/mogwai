#include "../src/runtime_choice.h"
#include <stdio.h>
#include <string.h>

static int total, pass;
static void chk(const char *name, int ok) {
    total++;
    if (ok) pass++;
    else printf("  FAIL runtime_choice_api: %s\n", name);
}

int main(void) {
    router_t r;
    runtime_choice_t out;
    runtime_candidate_t one[1] = {{"make coffee", 0, 0, 0}};
    runtime_candidate_t many[RUNTIME_CHOICE_MAX_CANDIDATES + 1];
    char too_long[RUNTIME_CHOICE_MAX_TEXT + 2];

    memset(&r, 0, sizeof r);
    memset(many, 0, sizeof many);
    for (int i = 0; i <= RUNTIME_CHOICE_MAX_CANDIDATES; i++) many[i].text = "x";
    memset(too_long, 'a', sizeof too_long);
    too_long[sizeof too_long - 1] = 0;

    chk("null output rejected", r_choose_runtime(&r, "x", one, 1, NULL) == -1);
    chk("null router rejected", r_choose_runtime(NULL, "x", one, 1, &out) == -1 && out.reason == RTC_REASON_BAD_ARGUMENT && out.winner == -1);
    chk("empty candidate set abstains", r_choose_runtime(&r, "x", NULL, 0, &out) == 0 && out.reason == RTC_REASON_NONE_NO_CANDIDATES && out.winner == -1);
    chk("too many candidates rejected", r_choose_runtime(&r, "x", many, RUNTIME_CHOICE_MAX_CANDIDATES + 1, &out) == -1 && out.reason == RTC_REASON_TOO_MANY_CANDIDATES && out.winner == -1);
    chk("missing candidate text rejected", r_choose_runtime(&r, "x", &(runtime_candidate_t){0}, 1, &out) == -1 && out.reason == RTC_REASON_MALFORMED_CANDIDATE && out.winner == -1);
    chk("overlong query rejected", r_choose_runtime(&r, too_long, one, 1, &out) == -1 && out.reason == RTC_REASON_MALFORMED_CANDIDATE && out.winner == -1);
    chk("valid scaffold fails closed", r_choose_runtime(&r, "brew espresso", one, 1, &out) == 0 && out.reason == RTC_REASON_UNSUPPORTED_SCORER && out.winner == -1 && out.score == 0 && out.margin == 0);

    printf("RUNTIME_CHOICE_API checks=%d/%d\n", pass, total);
    return pass == total ? 0 : 1;
}
