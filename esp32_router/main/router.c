#include "router.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

uint32_t r_fnv(const char *s, int n) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 16777619u; }
    return h;
}

int r_norm(const char *in, char *out, int cap) {
    int n = 0, sp = 1;
    out[n++] = ' ';
    for (const char *p = in; *p && n < cap - 2; p++) {
        char c = (char)tolower((unsigned char)*p);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) { out[n++] = c; sp = 0; }
        else if (!sp) { out[n++] = ' '; sp = 1; }
    }
    if (!sp) out[n++] = ' ';
    out[n] = 0;
    return n;
}

/* n-gram counts, integer. total is the L1 norm (no sqrt, no float). */
void r_counts(const char *text, int16_t *acc, int32_t *total) {
    char buf[512];
    int n = r_norm(text, buf, sizeof buf);
    memset(acc, 0, RD * sizeof *acc);
    int32_t t = 0;
    for (int g = 3; g <= 4; g++)
        for (int i = 0; i + g <= n; i++) { acc[r_fnv(buf + i, g) % RD]++; t++; }
    *total = t ? t : 1;
}

/* bit set iff  acc[i]/total  >=  centre[i]/RSCALE, cross-multiplied in int32 */
void r_encode(const router_t *r, const char *text, rvec *out) {
    int16_t acc[RD]; int32_t total;
    r_counts(text, acc, &total);
    memset(out, 0, sizeof *out);
    for (int i = 0; i < RD; i++)
        if ((int32_t)acc[i] * RSCALE >= r->centre[i] * total)
            out->w[i >> 5] |= 1u << (i & 31);
}

int r_sim(const rvec *a, const rvec *b) {
    int ham = 0;
    for (int i = 0; i < RWORDS; i++) ham += __builtin_popcount(a->w[i] ^ b->w[i]);
    return RD - 2 * ham;
}

/* ---- polarity ---- */
static int word_at(const char *h, const char *w) {
    size_t n = strlen(w);
    for (const char *p = strstr(h, w); p; p = strstr(p + 1, w))
        if (p > h && p[-1] == ' ' && (p[n] == ' ' || p[n] == 0)) return (int)(p - h);
    return -1;
}
static const char *POS[] = {"start","starts","enable","activate","brighter","brighten",
    "brightener","increase","raise","lighter","up",0};
static const char *NEG[] = {"off","stop","stops","disable","deactivate","kill","shut",
    "cut","cease","sleep","darkness","dim","dimmer","darker","lower","less","reduce",
    "down","no",0};
static const char *SWV[] = {"turn","switch","put","power","flip","pop","kick","get",0};

int r_polarity(const char *text) {
    char b[512]; int len = r_norm(text, b, sizeof b);
    int bp = -1, bn = -1, p;
    for (int i = 0; POS[i]; i++) { p = word_at(b, POS[i]); if (p >= 0 && (bp < 0 || p < bp)) bp = p; }
    for (int i = 0; NEG[i]; i++) { p = word_at(b, NEG[i]); if (p >= 0 && (bn < 0 || p < bn)) bn = p; }
    int on = word_at(b, "on");
    if (on >= 0) {
        int ok = (on + 3 >= len - 1);
        for (int i = 0; SWV[i] && !ok; i++) {
            int v = word_at(b, SWV[i]);
            if (v >= 0 && v < on && on - v <= 16) ok = 1;
        }
        if (ok && (bp < 0 || on < bp)) bp = on;
    }
    if (bp < 0 && bn < 0) return 0;
    if (bp >= 0 && bn >= 0) return bp < bn ? +1 : -1;
    return bp >= 0 ? +1 : -1;
}

/* polarity pairs, resolved by class NAME so the blob stays data-driven */
static int is_pos_side(const char *n) {
    return strstr(n, "lighton") || strstr(n, "wemo_on") || strstr(n, "lightup");
}
static int pair_of(const router_t *r, int c) {
    const char *n = r->names[c];
    const char *want = NULL;
    if      (strstr(n, "lighton"))  want = "lightoff";
    else if (strstr(n, "lightoff")) want = "lighton";
    else if (strstr(n, "wemo_on"))  want = "wemo_off";
    else if (strstr(n, "wemo_off")) want = "wemo_on";
    else if (strstr(n, "lightup"))  want = "lightdim";
    else if (strstr(n, "lightdim")) want = "lightup";
    if (!want) return -1;
    for (uint32_t i = 0; i < r->n_class; i++)
        if (strstr(r->names[i], want)) return (int)i;
    return -1;
}
int r_apply_polarity(const router_t *r, int cls, const char *text) {
    int sib = pair_of(r, cls);
    if (sib < 0) return cls;
    int pol = r_polarity(text);
    if (!pol) return cls;
    int want_pos = (pol > 0), is_pos = is_pos_side(r->names[cls]);
    return want_pos == is_pos ? cls : sib;
}
int r_family(const router_t *r, int cls) {
    if (cls < 0) return 0;
    const char *n = r->names[cls];
    if (strstr(n, "hue"))  return 1;
    if (strstr(n, "wemo")) return 2;
    return 3 + cls;
}

int r_route(const router_t *r, const char *text, int *score_out) {
    rvec q; r_encode(r, text, &q);
    int best = -RD - 1; uint32_t bi = 0;
    for (uint32_t i = 0; i < r->n_index; i++) {
        int s = r_sim(&q, &r->index[i]);
        if (s > best) { best = s; bi = i; }
    }
    if (score_out) *score_out = best;
    if (best <= r->threshold) return -1;
    int cls = r_apply_polarity(r, r->label[bi], text);
    int sb = -RD - 1, sc = -1;
    for (uint32_t c = 0; c < r->n_class; c++) {
        int s = r_sim(&q, &r->sig[c]);
        if (s > sb) { sb = s; sc = (int)c; }
    }
    if (r_family(r, sc) != r_family(r, cls)) return -1;   /* signature veto */
    return cls;
}
