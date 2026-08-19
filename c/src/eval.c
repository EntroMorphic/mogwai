/* eval.c — load blob, tune threshold on the held-out half of validation,
 * evaluate once on test. Reports the error taxonomy. Integer only. */
#include "router.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int json_str(const char *line, const char *key, char *out, int cap) {
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(line, pat); if (!p) return 0;
    p += strlen(pat); while (*p == ' ') p++; if (*p != '"') return 0; p++;
    int n = 0;
    while (*p && *p != '"' && n < cap - 1) { if (*p=='\\'&&p[1]) p++; out[n++] = *p++; }
    out[n] = 0; return 1;
}
#define MAXE 6000
static char *E_text[MAXE]; static char E_lab[MAXE][RNAMELEN]; static int E_n;
static void load(const char *path, int iot_parity) {
    FILE *f = fopen(path, "r"); if (!f) { perror(path); exit(1); }
    char line[4096], t[512], l[RNAMELEN]; int vi = 0; E_n = 0;
    while (fgets(line, sizeof line, f)) {
        if (!json_str(line, "text", t, sizeof t) || !json_str(line, "label_text", l, sizeof l)) continue;
        int iot = strncmp(l, "iot_", 4) == 0;
        if (iot_parity >= 0) {                    /* validation: take the odd half + negatives */
            if (iot) { if (vi++ % 2 == 0) continue; }
        }
        E_text[E_n] = strdup(t);
        snprintf(E_lab[E_n], RNAMELEN, "%s", iot ? l : "none");
        if (++E_n >= MAXE) break;
    }
    fclose(f);
}
int r_load(router_t *r, const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    fread(&r->magic,4,1,f); fread(&r->dim,4,1,f); fread(&r->n_index,4,1,f); fread(&r->n_class,4,1,f);
    if (r->magic != RMAGIC || r->dim != RD) { fclose(f); return -2; }
    fread(r->names, RNAMELEN, RMAXCLS, f);
    fread(r->centre, sizeof(int32_t), RD, f);
    fread(&r->threshold, sizeof(int32_t), 1, f);
    fread(r->sig, sizeof(rvec), RMAXCLS, f);
    r->index = malloc((size_t)r->n_index * sizeof(rvec));
    r->label = malloc(r->n_index);
    fread(r->index, sizeof(rvec), r->n_index, f);
    fread(r->label, 1, r->n_index, f);
    fclose(f); return 0;
}
void r_free(router_t *r){ free(r->index); free(r->label); }

typedef struct { int correct, false_act, wrong_act, missed; } tax_t;
static tax_t run(router_t *r, int th) {
    tax_t x = {0,0,0,0};
    int saved = r->threshold; r->threshold = th;
    for (int i = 0; i < E_n; i++) {
        int c = r_route(r, E_text[i], NULL);
        const char *pred = c < 0 ? "none" : r->names[c];
        int gold_none = !strcmp(E_lab[i], "none"), pred_none = !strcmp(pred, "none");
        if (!strcmp(pred, E_lab[i])) x.correct++;
        else if (gold_none) x.false_act++;
        else if (pred_none) x.missed++;
        else x.wrong_act++;
    }
    r->threshold = saved; return x;
}
int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr,"usage: eval <blob> <val.json> <test.json>\n"); return 1; }
    router_t r; if (r_load(&r, argv[1])) { fprintf(stderr,"bad blob\n"); return 1; }
    fprintf(stderr,"  index %u vectors, %u classes, %.1f KB resident\n",
            r.n_index, r.n_class, r.n_index*sizeof(rvec)/1024.0);
    load(argv[2], 1);
    int best_th = 0; long best_cost = 1L<<60;
    for (int th = -200; th <= 400; th += 4) {
        tax_t x = run(&r, th);
        long cost = 3L*(x.false_act + x.wrong_act) + x.missed;
        if (cost < best_cost) { best_cost = cost; best_th = th; }
    }
    fprintf(stderr,"  dev-tuned threshold: %d\n", best_th);
    load(argv[3], -1);
    tax_t x = run(&r, best_th);
    int iot_n = 0, iot_ok = 0;
    int saved = r.threshold; r.threshold = best_th;
    for (int i = 0; i < E_n; i++) {
        if (!strcmp(E_lab[i], "none")) continue;
        iot_n++;
        int c = r_route(&r, E_text[i], NULL);
        if (c >= 0 && !strcmp(r.names[c], E_lab[i])) iot_ok++;
    }
    r.threshold = saved;
    printf("  test n=%d   iot n=%d\n", E_n, iot_n);
    printf("  iot accuracy      %.1f%%\n", 100.0*iot_ok/iot_n);
    printf("  wrong actuations  %d  (false %d + wrong-intent %d)\n",
           x.false_act + x.wrong_act, x.false_act, x.wrong_act);
    printf("  missed            %d\n", x.missed);
    r_free(&r); return 0;
}
