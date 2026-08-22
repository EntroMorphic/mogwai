/* eval.c — verify a built router.bin on the host, before flashing it.
 *
 * REWRITTEN. The previous version had two serious faults:
 *   1. It parsed an OBSOLETE blob layout — threshold at the wrong offset, a
 *      `sig[]` section that no longer exists, and 32-byte rvec where the blob
 *      stores 64-byte tvec. It did not crash; it printed plausible numbers from
 *      misread bytes, which is worse.
 *   2. It loaded test.json and reported test figures WITHOUT touching
 *      results/TEST_BUDGET — exactly what the budget exists to prevent.
 *
 * It now reads the current format (see doc/BLOB_FORMAT.md), evaluates on
 * VALIDATION only at the blob's own threshold, and re-checks the embedded
 * reference queries. It cannot touch the test set; use `make testset` for that.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "router.h"
#include "ternary.h"

static char *V_t[4000]; static char V_l[4000][RNAMELEN]; static int V_n;
static router_t R; static tvec *TI; static uint16_t *ACT;
static const unsigned char *REFP; static uint32_t NREF;
static unsigned char *BLOB;

static int js(const char*l,const char*k,char*o,int cap){
    char pat[64]; snprintf(pat,sizeof pat,"\"%s\":",k);
    const char*p=strstr(l,pat); if(!p)return 0; p+=strlen(pat);
    while (*p == ' ') p++;
    if (*p != '"') return 0;
    p++;
    int n=0; while(*p&&*p!='"'&&n<cap-1){if(*p=='\\'&&p[1])p++;o[n++]=*p++;} o[n]=0; return 1; }
static int isiot(const char*l){return !strncmp(l,"iot_",4);}

static int load_blob(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) return 1;
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    BLOB = malloc(sz);
    if (fread(BLOB, 1, sz, f) != (size_t)sz) { fclose(f); return 1; }
    fclose(f);
    long o = 0; uint32_t h[5]; memcpy(h, BLOB, 20); o = 20;
    if (h[0] != RMAGIC) { fprintf(stderr, "  bad magic %08x\n", h[0]); return 1; }
    if (h[1] != (uint32_t)RD) {
        fprintf(stderr, "  blob dim %u but this binary is RD=%d — rebuild to match\n", h[1], RD);
        return 1; }
    memset(&R, 0, sizeof R);
    R.magic=h[0]; R.dim=h[1]; R.n_index=h[2]; R.n_class=h[3]; R.threshold=(int32_t)h[4];
    memcpy(R.names, BLOB+o, (size_t)RMAXCLS*RNAMELEN); o += (long)RMAXCLS*RNAMELEN;
    memcpy(R.centre, BLOB+o, (size_t)RD*4);           o += (long)RD*4;
    R.label = BLOB + o;                                o += R.n_index;
    ACT = (uint16_t *)(BLOB + o);                      o += (long)R.n_index*2;
    TI  = (tvec *)(BLOB + o);                          o += (long)R.n_index*sizeof(tvec);
    memcpy(&NREF, BLOB+o, 4);                          o += 4;
    REFP = BLOB + o;
    return 0;
}
static int route(const char *txt, int *score_out) {
    tvec q; t_encode(&R, txt, &q);
    int aa = t_active(&q), best = -(1<<28); uint32_t bi = 0;
    for (uint32_t i = 0; i < R.n_index; i++) {
        int s = t_score_pre(&q, &TI[i], aa, ACT[i]);
        if (s > best) { best = s; bi = i; }
    }
    if (score_out) *score_out = best;
    if (best <= R.threshold) return -1;
    return r_apply_polarity(&R, R.label[bi], txt);
}
int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: eval <router.bin> <validation.json>\n"
                        "  Verifies a built blob on the host: reference-query parity\n"
                        "  plus validation accuracy at the blob's own threshold.\n"
                        "  Deliberately cannot evaluate on test — use `make testset`.\n");
        return 1; }
    if (load_blob(argv[1])) { fprintf(stderr, "  cannot read %s\n", argv[1]); return 1; }
    printf("  blob: %u vectors, %u classes, %.0f KB index, threshold %d\n",
           R.n_index, R.n_class, R.n_index*sizeof(tvec)/1024.0, R.threshold);

    /* 1. the embedded references must still reproduce on this host */
    const unsigned char *p = REFP; int okc = 0, oks = 0;
    for (uint32_t i = 0; i < NREF; i++) {
        unsigned char len = *p++; char txt[256];
        memcpy(txt, p, len); txt[len] = 0; p += len;
        int32_t hs; memcpy(&hs, p, 4); p += 4;
        signed char hc = (signed char)*p++;
        int sc, cl = route(txt, &sc);
        if (cl == hc) okc++;
        if (sc == hs) oks++;
    }
    printf("  reference parity: class %u/%u  score %u/%u  %s\n", okc, NREF, oks, NREF,
           (okc==(int)NREF && oks==(int)NREF) ? "EXACT" : "*** MISMATCH ***");

    /* 2. Index-integrity check — NOT an accuracy measurement.
     *
     * mkblob puts EVERY validation IoT utterance into the index (compare.c does
     * the same: "all val IoT -> index"). So routing validation IoT back is pure
     * memorisation and must come out at exactly 100%. That makes it a corruption
     * detector, not a score: anything below 100% means the index, the labels or
     * the encoder disagree with what was built.
     *
     * Real accuracy needs a split that is NOT in the index — `compare` carves
     * DEV out of train for exactly this reason. Do not read the number below as
     * performance. The earlier version of this file made precisely that mistake
     * in the other direction, reporting test figures with no budget accounting.
     *
     * Validation NEGATIVES are not indexed, so their fa count IS meaningful —
     * though it is a different negative set from compare's DEV. */
    FILE *f = fopen(argv[2], "r"); if (!f) { fprintf(stderr,"  cannot read %s\n", argv[2]); return 1; }
    char line[8192], t[512], l[RNAMELEN];
    while (fgets(line, sizeof line, f) && V_n < 4000)
        if (js(line,"text",t,sizeof t) && js(line,"label_text",l,sizeof l)) {
            V_t[V_n]=strdup(t); snprintf(V_l[V_n],RNAMELEN,"%s",isiot(l)?l:"none"); V_n++; }
    fclose(f);
    int iot=0, iok=0, fa=0, wa=0, ms=0;
    for (int i = 0; i < V_n; i++) {
        int c = route(V_t[i], NULL);
        const char *pred = c < 0 ? "none" : R.names[c];
        int gn = !strcmp(V_l[i], "none");
        if (!gn) { iot++; if (!strcmp(pred, V_l[i])) iok++; }
        if (!strcmp(pred, V_l[i])) continue;
        if (gn) fa++; else if (!strcmp(pred,"none")) ms++; else wa++;
    }
    double memr = iot ? 100.0*iok/iot : 0.0;
    printf("  index integrity : %d/%d indexed iot utterances route back (%.1f%%)"
           "  wa=%d missed=%d  %s\n", iok, iot, memr, wa, ms,
           (memr >= 99.99 && wa == 0 && ms == 0) ? "OK" : "*** INDEX CORRUPT ***");
    printf("  negatives       : fa=%d of %d unindexed negatives (%.2f%%)\n",
           fa, V_n - iot, 100.0*fa/(V_n - iot));
    printf("  NOTE: the iot line is memorisation, not accuracy - those utterances\n"
           "        are IN the index. Use `make ship` for a real dev number.\n");
    if (memr < 99.99 || wa || ms) return 3;
    return (okc==(int)NREF && oks==(int)NREF) ? 0 : 2;
}
