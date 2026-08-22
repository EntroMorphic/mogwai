/* build.c — construct the router blob from MASSIVE JSONL + NLU-Eval CSV.
 * Host tool. Integer only. */
#include "router.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXU 40000
static char  *g_text[MAXU];
static char   g_lab[MAXU][RNAMELEN];
static int    g_n = 0;

static char *dup_n(const char *s, int n) { char *d = malloc(n + 1); memcpy(d, s, n); d[n] = 0; return d; }

/* pull "key": "value" out of a flat JSON object line */
static int json_str(const char *line, const char *key, char *out, int cap) {
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(line, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ') p++;
    if (*p != '"') return 0;
    p++;
    int n = 0;
    while (*p && *p != '"' && n < cap - 1) {
        if (*p == '\\' && p[1]) p++;
        out[n++] = *p++;
    }
    out[n] = 0;
    return 1;
}
static void add(const char *text, const char *lab) {
    if (g_n >= MAXU || !text[0]) return;
    g_text[g_n] = dup_n(text, (int)strlen(text));
    snprintf(g_lab[g_n], RNAMELEN, "%s", lab);
    g_n++;
}
static int is_iot(const char *l) { return strncmp(l, "iot_", 4) == 0; }

static void load_jsonl(const char *path) {
    FILE *f = fopen(path, "r"); if (!f) { fprintf(stderr, "missing %s\n", path); exit(1); }
    char line[4096], t[512], l[RNAMELEN];
    while (fgets(line, sizeof line, f))
        if (json_str(line, "text", t, sizeof t) && json_str(line, "label_text", l, sizeof l))
            add(t, is_iot(l) ? l : "none");
    fclose(f);
}
/* NLU-Eval: semicolon separated, quoted; cols scenario(2) intent(3) answer(9) */
static void load_nlu(const char *path, const char *massive_test) {
    /* build a set of normalised test strings to exclude (MASSIVE derives from this) */
    static char *excl[8000]; int nx = 0;
    FILE *e = fopen(massive_test, "r"); char line[4096], t[512];
    while (e && fgets(line, sizeof line, e))
        if (json_str(line, "text", t, sizeof t)) {
            char nb[512]; r_norm(t, nb, sizeof nb);
            if (nx < 8000) excl[nx++] = dup_n(nb, (int)strlen(nb));
        }
    if (e) fclose(e);
    FILE *f = fopen(path, "r"); if (!f) { fprintf(stderr, "missing %s\n", path); return; }
    int added = 0, skipped = 0;
    while (fgets(line, sizeof line, f)) {
        char *fld[12] = {0}; int nf = 0; char *p = line; int inq = 0;
        fld[nf++] = p;
        for (; *p && nf < 12; p++) {
            if (*p == '"') inq = !inq;
            else if (*p == ';' && !inq) { *p = 0; fld[nf++] = p + 1; }
        }
        if (nf < 10) continue;
        for (int i = 0; i < nf; i++) {                       /* strip quotes */
            char *s = fld[i]; int L = (int)strlen(s);
            while (L && (s[L-1]=='\n'||s[L-1]=='\r')) s[--L]=0;
            if (L >= 2 && s[0]=='"' && s[L-1]=='"') { s[L-1]=0; fld[i] = s+1; }
        }
        if (strcmp(fld[2], "iot") != 0) continue;
        char nb[512]; r_norm(fld[9], nb, sizeof nb);
        int dup = 0;
        for (int i = 0; i < nx && !dup; i++) if (!strcmp(nb, excl[i])) dup = 1;
        if (dup) { skipped++; continue; }
        char lab[RNAMELEN]; snprintf(lab, sizeof lab, "iot_%s", fld[3]);
        add(fld[9], lab); added++;
    }
    fclose(f);
    fprintf(stderr, "  NLU-Eval: +%d iot, %d excluded as MASSIVE-test overlap\n", added, skipped);
}

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: build <train.json> <val.json> <test.json> <nlu.csv> <out.bin>\n"); return 1; }
    load_jsonl(argv[1]);
    int n_after_train = g_n;
    /* half of validation IoT joins the index; the rest is the tuning set */
    FILE *f = fopen(argv[2], "r"); char line[4096], t[512], l[RNAMELEN]; int vi = 0;
    while (f && fgets(line, sizeof line, f))
        if (json_str(line, "text", t, sizeof t) && json_str(line, "label_text", l, sizeof l))
            if (is_iot(l) && (vi++ % 2 == 0)) add(t, l);
    if (f) fclose(f);
    load_nlu(argv[4], argv[3]);
    fprintf(stderr, "  index: %d (%d from MASSIVE train)\n", g_n, n_after_train);

    router_t r; memset(&r, 0, sizeof r);
    r.magic = RMAGIC; r.dim = RD; r.n_index = g_n;
    /* class table */
    for (int i = 0; i < g_n; i++) {
        int found = -1;
        for (uint32_t c = 0; c < r.n_class; c++) if (!strcmp(r.names[c], g_lab[i])) { found = (int)c; break; }
        if (found < 0 && r.n_class < RMAXCLS) { snprintf(r.names[r.n_class], RNAMELEN, "%.*s", RNAMELEN-1, g_lab[i]); r.n_class++; }
    }
    /* integer centre: mean over the index of count_i/total, in RSCALE units */
    int64_t sum[RD]; memset(sum, 0, sizeof sum);
    int16_t acc[RD]; int32_t tot;
    for (int i = 0; i < g_n; i++) {
        r_counts(g_text[i], acc, &tot);
        for (int d = 0; d < RD; d++) sum[d] += ((int64_t)acc[d] * RSCALE) / tot;
    }
    for (int d = 0; d < RD; d++) r.centre[d] = (int32_t)(sum[d] / g_n);

    r.index = calloc(g_n, sizeof(rvec));
    r.label = calloc(g_n, 1);
    for (int i = 0; i < g_n; i++) {
        r_encode(&r, g_text[i], &r.index[i]);
        for (uint32_t c = 0; c < r.n_class; c++) if (!strcmp(r.names[c], g_lab[i])) { r.label[i] = (uint8_t)c; break; }
    }
    /* signature: per class, majority bit */
    for (uint32_t c = 0; c < r.n_class; c++) {
        int cnt = 0; static int ones[RD];
        memset(ones, 0, sizeof ones);
        for (int i = 0; i < g_n; i++) if (r.label[i] == c) {
            cnt++;
            for (int d = 0; d < RD; d++) if (r.index[i].w[d >> 5] & (1u << (d & 31))) ones[d]++;
        }
        for (int d = 0; d < RD; d++) if (cnt && ones[d] * 2 >= cnt) r.sig[c].w[d >> 5] |= 1u << (d & 31);
    }
    r.threshold = 0;                                     /* set by tune */
    FILE *o = fopen(argv[5], "wb");
    fwrite(&r.magic, 4, 1, o); fwrite(&r.dim, 4, 1, o);
    fwrite(&r.n_index, 4, 1, o); fwrite(&r.n_class, 4, 1, o);
    fwrite(r.names, RNAMELEN, RMAXCLS, o);
    fwrite(r.centre, sizeof(int32_t), RD, o);
    fwrite(&r.threshold, sizeof(int32_t), 1, o);
    fwrite(r.sig, sizeof(rvec), RMAXCLS, o);
    fwrite(r.index, sizeof(rvec), g_n, o);
    fwrite(r.label, 1, g_n, o);
    long bytes = ftell(o); fclose(o);
    fprintf(stderr, "  wrote %s: %ld bytes (%.2f KB), %u classes\n", argv[5], bytes, bytes/1024.0, r.n_class);
    return 0;
}
