#include "invariants.h"
#include "router.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static int g_viol = 0;
int inv_violations(void){ return g_viol; }

#define ABORT(...) do { \
    fprintf(stderr, "\n  *** INVARIANT VIOLATED ***\n  "); \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n  The design guarantees this cannot happen, so a\n" \
                    "  measurement showing it is a bug, not a finding.\n" \
                    "  Refusing to report. Fix the cause.\n\n"); \
    g_viol++; exit(2); } while (0)

#define HN 262144
static char *SET[HN];
static void set_clear(void){ for(int i=0;i<HN;i++){ free(SET[i]); SET[i]=NULL; } }
static void set_add(const char *s){
    char b[512]; r_norm(s,b,sizeof b);
    uint32_t h=r_fnv(b,(int)strlen(b))%HN;
    while(SET[h]){ if(!strcmp(SET[h],b)) return; h=(h+1)%HN; }
    SET[h]=strdup(b);
}
static const char *set_hit(const char *s){
    char b[512]; r_norm(s,b,sizeof b);
    uint32_t h=r_fnv(b,(int)strlen(b))%HN;
    while(SET[h]){ if(!strcmp(SET[h],b)) return SET[h]; h=(h+1)%HN; }
    return NULL;
}
/* index and evaluation sets must share no utterance, by normalised string */
void inv_disjoint(const char *what, char **a, int na, char **b, int nb) {
    set_clear();
    for (int i = 0; i < na; i++) set_add(a[i]);
    int hits = 0; const char *ex = NULL;
    for (int i = 0; i < nb; i++) { const char *h = set_hit(b[i]); if (h) { hits++; if(!ex) ex = b[i]; } }
    if (hits)
        ABORT("%s: %d of %d evaluation utterances (%.1f%%) are present in the index.\n"
              "  e.g. \"%s\"", what, hits, nb, 100.0*hits/nb, ex);
    fprintf(stderr, "  [inv] %-28s disjoint (%d vs %d)\n", what, na, nb);
}
/* a representation that CONTAINS another cannot score lower than it */
void inv_superset(const char *sub, double sub_acc, const char *sup, double sup_acc, double tol) {
    if (sup_acc < sub_acc - tol)
        ABORT("%s (%.1f%%) scored below %s (%.1f%%), but %s contains %s:\n"
              "  setting every mask bit reduces it exactly to %s.",
              sup, sup_acc, sub, sub_acc, sup, sub, sub);
    fprintf(stderr, "  [inv] %-28s %.1f%% >= %s %.1f%%\n", "superset holds", sup_acc, sub, sub_acc);
}
void inv_similar_rate(const char *what, double a, double b, double tol_pts) {
    double d = a > b ? a - b : b - a;
    if (d > tol_pts)
        ABORT("%s: dev %.1f%% vs test %.1f%% differ by %.1f points (tol %.1f).\n"
              "  The splits are not comparable; dev cannot predict test.", what, a, b, d, tol_pts);
    fprintf(stderr, "  [inv] %-28s dev %.1f%% ~ test %.1f%%\n", what, a, b);
}
void inv_bounded(const char *what, long v, long lo, long hi) {
    if (v < lo || v > hi) ABORT("%s = %ld outside [%ld, %ld]", what, v, lo, hi);
}
