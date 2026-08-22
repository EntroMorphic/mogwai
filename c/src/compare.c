/* compare.c — binary vs twin-ternary vs cascade. Integer only.
 * Each router scores a query ONCE; thresholds sweep cached scores. */
#include "ternary.h"
#include "cascade.h"
#include "invariants.h"
#include "prior.h"
#include "prune.h"
#include "cue.h"
#include "gate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define MAXU 40000
static char *U_t[MAXU]; static char U_l[MAXU][RNAMELEN]; static int U_n;
static char *V_t[3000]; static char V_l[3000][RNAMELEN]; static int V_n;
static char *T_t[4000]; static char T_l[4000][RNAMELEN]; static int T_n;
static router_t R; static tvec *TI; static tvec TSIG[RMAXCLS];
static int SIGMODE = 0;  /* re-open: mask choice also came from leaked dev */
static int NOVETO  = 1;   /* signature veto DROPPED: hash-derived, measured inert */
static int ATTEN   = 1;   /* prior yields: score >>= ATTEN on disagreement */
static prior_t PR;
static int FIXTH = 1<<30;  /* --fixth=N: skip tuning, force this threshold */
static int CURVE = 0;
static int DUMPERR = 0;   /* --errs: print the actual misclassified utterances */
static int DIRDUMP = 0;   /* --dirdump: per-item score/pred/truth, for direction analysis */
static int LEAKTEST = 0; /* --leak: reintroduce the bug on purpose, to verify the guard */
static prune_opt PRUNE = {0,0,0};
static cue_t CUE; static int USECUE = 0;   /* --cue: index-derived hard word cues */
static int CHANNELS = 0;   /* --channels: word vs n-gram error overlap */
static int SELDUMP = 0;   /* --seldump: per-item signals a selector could use */
static int XVAL = 0;      /* --xval: 2-fold CV inside the index */
static int GATESZ = 0;    /* --gatesize: what a compact prior table needs */
static int DENSITY = 0;   /* --density: how sparse are the index vectors? */
static int GATECHK = 0;   /* --gatecheck: gate must equal prior bit-exactly */
static gate_t GATE; static int USEGATE = 0;  /* --gatesel: selector reads the compact table */
static int SELSIG = 0;    /* --selsig: paired significance of the dev selector gain */
static const char *ROUTE1 = 0;  /* --route="..." */
static int REPL = 0;            /* --repl */
static int PRIORCLS = 0;  /* --priorcls: router accepts/rejects, prior picks the class */
static int SELMARG = 0;   /* --selmargin=N: prior votes only when its margin >= N */

static int js(const char*l,const char*k,char*o,int cap){
    char pat[64]; snprintf(pat,sizeof pat,"\"%s\":",k);
    const char*p=strstr(l,pat); if(!p)return 0; p+=strlen(pat);
    while (*p == ' ') p++;
    if (*p != '"') return 0;
    p++;
    int n=0; while(*p&&*p!='"'&&n<cap-1){if(*p=='\\'&&p[1])p++;o[n++]=*p++;} o[n]=0; return 1; }
static int isiot(const char*l){return !strncmp(l,"iot_",4);}
static void push(char**ta,char la[][RNAMELEN],int*n,const char*t,const char*l){
    ta[*n]=strdup(t); snprintf(la[*n],RNAMELEN,"%s",l); (*n)++; }
#define HN 65536
static char *HS[HN];
static void hs_add(const char*s){ char b[512]; r_norm(s,b,sizeof b);
    uint32_t h=r_fnv(b,(int)strlen(b))%HN;
    while(HS[h]){ if(!strcmp(HS[h],b))return; h=(h+1)%HN; } HS[h]=strdup(b); }
static int hs_has(const char*s){ char b[512]; r_norm(s,b,sizeof b);
    uint32_t h=r_fnv(b,(int)strlen(b))%HN;
    while(HS[h]){ if(!strcmp(HS[h],b))return 1; h=(h+1)%HN; } return 0; }

/* each scorer returns the best score and the class it would emit (pre-threshold) */
typedef struct { int score, cls; } hit;
static int veto(const tvec *q,int aa,int cls){
    if(NOVETO) return cls;
    int sb=-(1<<28),sc=-1;
    for(uint32_t c=0;c<R.n_class;c++){int s=t_score(q,&TSIG[c],aa); if(s>sb){sb=s;sc=(int)c;}}
    return r_family(&R,sc)!=r_family(&R,cls) ? -1 : cls;
}
static hit score_bin(const char*txt){
    rvec q; r_encode(&R,txt,&q);
    int best=-RD-1; uint32_t bi=0;
    for(uint32_t i=0;i<R.n_index;i++){int s=r_sim(&q,&R.index[i]); if(s>best){best=s;bi=i;}}
    int cls=r_apply_polarity(&R,R.label[bi],txt);
    int sb=-RD-1,sc=-1;
    for(uint32_t c=0;c<R.n_class;c++){int s=r_sim(&q,&R.sig[c]); if(s>sb){sb=s;sc=(int)c;}}
    hit h={best, r_family(&R,sc)!=r_family(&R,cls)?-1:cls}; return h;
}
static hit score_ter_impl(const char*txt,int use_prior){
    tvec q; t_encode(&R,txt,&q); int aa=t_active(&q);
    int best=-(1<<28); uint32_t bi=0;
    for(uint32_t i=0;i<R.n_index;i++){int s=t_score(&q,&TI[i],aa); if(s>best){best=s;bi=i;}}
    int cls0=r_apply_polarity(&R,R.label[bi],txt);
    if(USECUE) cls0=cue_apply(&CUE,&R,txt,cls0);
    /* Channel split: the router is the EVIDENCE READER and decides accept vs
       reject (fa=1 of 1335 — it is good at that). The word prior is a separate
       channel that is better at WHICH IoT class (89.6%% vs 85.9%% on dev IoT,
       with 16 items the router gets wrong that it gets right). Let each do the
       job it is better at, rather than blending scores as the earlier prior
       work did. */
    if(PRIORCLS){ int mg; int pv=pr_vote(&PR,txt,&mg);
        /* The router must have already decided this is a command: never let the
           prior turn a "none" into an actuation. Without this guard fa went
           1 -> 824, because every above-threshold negative got reassigned. */
        /* SELECTOR: the prior votes only when it has a margin. prmarg==0 means it
           matched no discriminating word — an abstention, not a weak opinion.
           Gate cross-validated inside the index on 183 disagreement items. */
        if(USEGATE) pv = gate_vote(&GATE, txt, &mg);
        if(pv>=0 && cls0>=0 && mg>=SELMARG && strcmp(R.names[cls0],"none")
            && strcmp(R.names[pv],"none")
            && (PRIORCLS>=2 || r_family(&R,pv)==r_family(&R,cls0))) cls0=pv; }
    if(use_prior){
        int mg=0, pc=pr_vote(&PR,txt,&mg);
        /* The prior abstains when no word carries evidence (pc<0), and it never
           overrides: on disagreement it only attenuates and the threshold
           adjudicates. No imported constant - the trit-count threshold from
           the-reflex is meaningless in these units and never fired. */
        if(pc>=0 && r_family(&R,pc)!=r_family(&R,cls0)) best >>= ATTEN;
        (void)mg;
    }
    hit h; h.score=best; h.cls=cls0; return h;
}
static hit score_ter(const char*txt){ return score_ter_impl(txt,0); }
static hit score_ter_wp(const char*txt){ return score_ter_impl(txt,1); } /* --curve only */
/* stage 1 REORDERS (counting sort on mask overlap), never rejects.
 * stage 2 reranks the survivors against the stored text. */
static hit score_cas(const char*txt){
    tvec q; t_encode(&R,txt,&q); int aa=t_active(&q);
    static int ov[MAXU]; int cnt[RD+2]; memset(cnt,0,sizeof cnt);
    for(uint32_t i=0;i<R.n_index;i++){ ov[i]=c_mask_overlap(&q,&TI[i]); cnt[ov[i]]++; }
    int acc=0,cut=RD;
    for(; cut>=0; cut--){ acc+=cnt[cut]; if(acc>=CAS_K) break; }
    int best=-(1<<28),bi=-1;
    for(uint32_t i=0;i<R.n_index;i++){
        if(ov[i]<cut) continue;   /* true cut, no index-order truncation */
        int s=t_score(&q,&TI[i],aa)+c_text_sim(txt,U_t[i]);
        if(s>best){best=s;bi=(int)i;}
    }
    if(bi<0){ hit h={-(1<<28),-1}; return h; }
    hit h={best, veto(&q,aa,r_apply_polarity(&R,R.label[bi],txt))}; return h;
}
typedef struct{int fa,wa,ms,iok,in;} TX;
static TX tally(hit*H,int th,char la[][RNAMELEN],int n){
    TX z={0,0,0,0,0};
    for(int i=0;i<n;i++){
        int c = (H[i].score>th) ? H[i].cls : -1;
        const char *p = c<0 ? "none" : R.names[c];
        int gn=!strcmp(la[i],"none");
        if(!gn){ z.in++; if(!strcmp(p,la[i])) z.iok++; }
        if(!strcmp(p,la[i])) continue;
        if(gn) z.fa++; else if(!strcmp(p,"none")) z.ms++; else z.wa++;
    } return z;
}
static hit *precompute(hit(*f)(const char*),char**ta,int n){
    hit *H=malloc(n*sizeof(hit));
    for(int i=0;i<n;i++) H[i]=f(ta[i]);
    return H;
}
static int tune(hit*H,char la[][RNAMELEN],int n,int lo,int hi){
    int best=lo; long bc=1L<<60;
    for(int th=lo;th<=hi;th+=2){ TX z=tally(H,th,la,n);
        long c=3L*(z.fa+z.wa)+z.ms; if(c<bc){bc=c;best=th;} }
    return best;
}
static int USE_TEST = 0;
static hit *LAST = NULL; static int LAST_TH = 0; static const char *LAST_NAME = "";

/* paired McNemar on the wrong-action event. No tuning involved, so not leakable. */
static void mcnemar(hit *A, int ta, hit *B, int tb, char la[][RNAMELEN], int n) {
    int fixed = 0, broke = 0;
    for (int i = 0; i < n; i++) {
        int ca = (A[i].score > ta) ? A[i].cls : -1;
        int cb = (B[i].score > tb) ? B[i].cls : -1;
        const char *pa = ca < 0 ? "none" : R.names[ca];
        const char *pb = cb < 0 ? "none" : R.names[cb];
        int gn = !strcmp(la[i], "none");
        int wa = (gn && strcmp(pa, "none")) || (!gn && strcmp(pa, "none") && strcmp(pa, la[i]));
        int wb = (gn && strcmp(pb, "none")) || (!gn && strcmp(pb, "none") && strcmp(pb, la[i]));
        if (wa && !wb) fixed++; else if (wb && !wa) broke++;
    }
    /* Overall correctness, with an exact p. The wrong-axis counts above say
       which errors moved; they do NOT say whether the change is real. The core
       twin-vs-binary claim went into a public README carrying only a gap and a
       saturation argument, because nothing computed this. Now everything does. */
    int cfix = 0, cbrk = 0;
    for (int i = 0; i < n; i++) {
        int ca = (A[i].score > ta) ? A[i].cls : -1;
        int cb = (B[i].score > tb) ? B[i].cls : -1;
        int oa = !strcmp(ca < 0 ? "none" : R.names[ca], la[i]);
        int ob = !strcmp(cb < 0 ? "none" : R.names[cb], la[i]);
        if (ob && !oa) cfix++; else if (oa && !ob) cbrk++;
    }
    int m = cfix + cbrk; double s = 0, tot = 0;
    int lo = cfix < cbrk ? cfix : cbrk, hi = cfix > cbrk ? cfix : cbrk;
    for (int k = 0; k <= m; k++) {
        double w = 1; for (int j = 0; j < k; j++) w = w * (m - j) / (j + 1);
        tot += w; if (k <= lo || k >= hi) s += w;
    }
    double p = m ? s / tot : 1.0;
    printf("      vs %-18s wrong: fixed %-3d broke %-3d %s\n",
           LAST_NAME, fixed, broke, broke == 0 ? "(non-destructive)" : "");
    printf("      %-21s overall: fixed %-3d broke %-3d  p=%.4f %s\n", "",
           cfix, cbrk, p, p < 0.05 ? "SIGNIFICANT" : "not significant");
    printf("PAIR\t%s\t%d\t%d\t%.6f\n", LAST_NAME, cfix, cbrk, p);
}
/* Does the prior MOVE the operating curve, or merely slide along it?
 * If its (wrong, missed) frontier sits on top of the no-prior frontier, it is
 * 82.9%-accurate machinery duplicating a scalar we already have. */
static void curve(const char *name, hit (*f)(const char *), int lo, int hi) {
    hit *H = precompute(f, V_t, V_n);
    for (int th = lo; th <= hi; th += 2) {
        TX z = tally(H, th, V_l, V_n);
        /* fa and wa are different failures for an actuator: fa fires on a
           NON-command (unbidden), wa acts wrongly on a real command. Emit
           separately — collapsing them hides the safety-critical one. */
        printf("CURVE\t%s\t%d\t%d\t%d\t%d\t%d\t%d\n", name, th,
               z.fa + z.wa, z.ms, z.fa, z.wa, z.iok);
    }
    free(H);
}
/* Exact-string disjointness (inv_disjoint) is necessary but not sufficient:
   two different strings that encode to the SAME twin-ternary code are
   indistinguishable to the router, so a test item sharing a code with an index
   entry is effectively leaked even though no assertion fires. Also reports the
   IoT counts the error bars are computed from. */
/* Exact-string disjointness is necessary but not sufficient: two different
   strings that encode to the SAME twin-ternary code are indistinguishable to
   the router, so a test item sharing a code with an index entry is effectively
   leaked with no assertion firing. */
enum { CO_HB = 1 << 15 };
static int co_head[CO_HB], co_nxt[40000], co_built = 0;
static void co_build(void) {
    if (co_built) return;
    for (int i = 0; i < CO_HB; i++) co_head[i] = -1;
    for (int i = 0; i < U_n; i++) {
        const unsigned char *b = (const unsigned char *)&TI[i];
        uint32_t h = 2166136261u;
        for (size_t k = 0; k < sizeof(tvec); k++) { h ^= b[k]; h *= 16777619u; }
        uint32_t s = h & (CO_HB - 1); co_nxt[i] = co_head[s]; co_head[s] = i;
    }
    co_built = 1;
}
/* THE single collision test. The self-check below and the reporting loop must
   both go through this. An earlier version of the self-check had its own copy
   of the memcmp, so disabling the real one left the self-check green — the
   control validated a parallel implementation instead of the live one. */
static int co_collides(const tvec *q) {
    const unsigned char *b = (const unsigned char *)q;
    uint32_t h = 2166136261u;
    for (size_t k = 0; k < sizeof(tvec); k++) { h ^= b[k]; h *= 16777619u; }
    for (int j = co_head[h & (CO_HB - 1)]; j >= 0; j = co_nxt[j])
        if (!memcmp(&TI[j], q, sizeof(tvec))) return 1;
    return 0;
}
static void code_overlap(const char *tag, char **X, char lx[][RNAMELEN], int nx) {
    co_build();
    /* POSITIVE CONTROL: "0 overlaps" from a broken detector looks identical to
       "0 overlaps" from a clean split. Prove it can fire first — an index
       utterance must collide with its own stored code. */
    if (U_n > 0) {
        tvec p; t_encode(&R, U_t[0], &p);
        if (!co_collides(&p)) {
            fprintf(stderr,
                "  [diag] SELFTEST FAILED: the code-overlap detector cannot find an\n"
                "         index utterance in its own index. It is broken, so any\n"
                "         \"0 overlaps\" it reports is meaningless.\n");
            exit(2);
        }
    }
    int iot = 0, coll = 0, coll_iot = 0;
    for (int i = 0; i < nx; i++) {
        int is_iot = strcmp(lx[i], "none") != 0;
        iot += is_iot;
        tvec q; t_encode(&R, X[i], &q);
        if (co_collides(&q)) { coll++; coll_iot += is_iot; }
    }
    fprintf(stderr, "  [diag] %-5s n=%-5d iot=%-4d | code-identical to an index entry: "
                    "%d (%.2f%%), of which iot %d\n", tag, nx, iot, coll, 100.0*coll/nx, coll_iot);
}
/* Dump the actual misclassifications at a given threshold. With fa down to a
   single event, aggregate counts stop being informative — you have to look at
   the utterance. */
static void dump_errs(hit *H, int th, char la[][RNAMELEN], char **X, int n) {
    fprintf(stderr, "\n  --- dev errors at th=%d ---\n", th);
    for (int i = 0; i < n; i++) {
        int c = (H[i].score > th) ? H[i].cls : -1;
        const char *p = c < 0 ? "none" : R.names[c];
        int gn = !strcmp(la[i], "none");
        if (!strcmp(p, la[i])) continue;
        const char *kind = gn ? "UNBIDDEN" : (!strcmp(p, "none") ? "missed" : "wrong-act");
        if (gn || strcmp(p, "none"))       /* fa and wa only; misses are quiet */
            fprintf(stderr, "  %-10s score=%-4d  said=%-16s truth=%-16s  \"%s\"\n",
                    kind, H[i].score, p, la[i], X[i]);
    }
}
/* --dirdump: one TSV line per dev item — score, predicted class, true label.
 * Lets per-direction operating curves be computed without a second threshold
 * ever entering the router. If up-commands and down-commands do NOT have
 * materially different score distributions, an asymmetric threshold is not
 * structure, it is a second parameter fitted to 192 dev items. */
static void dirdump(hit *H, char la[][RNAMELEN], int n) {
    for (int i = 0; i < n; i++)
        printf("DD\t%d\t%s\t%s\n", H[i].score,
               H[i].cls < 0 ? "none" : R.names[H[i].cls], la[i]);
}
/* --channels: are the word channel and the n-gram channel complementary?
 * Three separate attempts to add word information have now failed (signature
 * prior inert, word prior inert, hard cue harmful) even though the word prior
 * ALONE scores 82.9% against the router's 85.9%. If the two channels fail on
 * the SAME items, that explains all three results at once: the second channel
 * carries no information the first lacks, so no combination rule can help. */
static void channels(void) {
    int both=0, only_r=0, only_p=0, neither=0, n=0;
    for (int i = 0; i < V_n; i++) {
        if (!strcmp(V_l[i], "none")) continue;
        n++;
        hit h = score_ter(V_t[i]);
        int rc = (h.score > 136) ? h.cls : -1;
        int rok = rc >= 0 && !strcmp(R.names[rc], V_l[i]);
        int mg; int pv = pr_vote(&PR, V_t[i], &mg);
        int pok = pv >= 0 && !strcmp(R.names[pv], V_l[i]);
        if (rok && pok) both++; else if (rok) only_r++;
        else if (pok) only_p++; else neither++;
    }
    printf("\n  === channel overlap on %d dev IoT items ===\n", n);
    printf("    both correct        %4d  (%.1f%%)\n", both, 100.0*both/n);
    printf("    router only         %4d\n", only_r);
    printf("    word-prior only     %4d   <- what a second channel could ADD\n", only_p);
    printf("    neither             %4d   <- the irreducible floor\n", neither);
    printf("    router  total %d (%.1f%%)   prior total %d (%.1f%%)\n",
           both+only_r, 100.0*(both+only_r)/n, both+only_p, 100.0*(both+only_p)/n);
    printf("    an ORACLE picking the right channel per item: %d (%.1f%%)\n",
           both+only_r+only_p, 100.0*(both+only_r+only_p)/n);
}
/* --seldump: everything a SELECTOR could possibly condition on, per dev IoT
 * item, with which channel was actually right. A selector must use signals
 * available at inference time — so: router top score, router margin (top minus
 * best-scoring-different-class), prior margin, whether the channels agree,
 * whether a polarity cue fired, and utterance length. */
static void seldump(void) {
    printf("SEL\ttop\tmargin\tprmarg\tagree\tpol\twords\trok\tpok\ttruth\n");
    for (int i = 0; i < V_n; i++) {
        if (!strcmp(V_l[i], "none")) continue;
        tvec q; t_encode(&R, V_t[i], &q); int aa = t_active(&q);
        int b1 = -(1<<28), b2 = -(1<<28); uint32_t bi = 0;
        for (uint32_t k = 0; k < R.n_index; k++) {
            int s = t_score(&q, &TI[k], aa);
            if (s > b1) { b2 = b1; b1 = s; bi = k; }
            else if (s > b2 && R.label[k] != R.label[bi]) b2 = s;
        }
        int rc = r_apply_polarity(&R, R.label[bi], V_t[i]);
        int pol = r_polarity(V_t[i]);
        int mg; int pv = pr_vote(&PR, V_t[i], &mg);
        int rok = rc >= 0 && !strcmp(R.names[rc], V_l[i]);
        int pok = pv >= 0 && !strcmp(R.names[pv], V_l[i]);
        int agree = (rc >= 0 && pv >= 0 && rc == pv);
        int w = 1; for (const char *p = V_t[i]; *p; p++) if (*p == ' ') w++;
        printf("SEL\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\n",
               b1, b1 - b2, mg, agree, pol, w, rok, pok, V_l[i]);
    }
}
/* --xval: 2-fold cross-validation INSIDE the index, to test whether the
 * prior-margin signal is real or fitted to 20 dev items.
 *
 * For each fold: rebuild the centre, the index vectors and the word prior from
 * the OTHER half only, then evaluate this half's IoT items. Nothing from the
 * evaluated half informs the model. Yields ~10x more disagreement items than
 * the dev slice the threshold was chosen on. */
static void xval(void) {
    int n = U_n;
    static uint8_t fold[40000];
    for (int i = 0; i < n; i++) fold[i] = i & 1;          /* deterministic */
    printf("XV\tprmarg\ttop\trok\tpok\n");
    static char *tr[40000]; static uint8_t trl[40000];
    for (int f = 0; f < 2; f++) {
        int m = 0;
        for (int i = 0; i < n; i++) if (fold[i] != f) { tr[m] = U_t[i]; trl[m] = R.label[i]; m++; }
        /* centre from the training half only */
        router_t S; memset(&S, 0, sizeof S);
        S.magic = RMAGIC; S.dim = RD; S.n_class = R.n_class;
        memcpy(S.names, R.names, sizeof R.names);
        int64_t sum[RD]; memset(sum, 0, sizeof sum);
        int16_t acc[RD]; int32_t tot;
        for (int i = 0; i < m; i++) { r_counts(tr[i], acc, &tot);
            for (int d = 0; d < RD; d++) sum[d] += ((int64_t)acc[d] * RSCALE) / tot; }
        for (int d = 0; d < RD; d++) S.centre[d] = (int32_t)(sum[d] / m);
        S.n_index = m; S.label = trl;
        tvec *TS = malloc((size_t)m * sizeof(tvec));
        for (int i = 0; i < m; i++) t_encode(&S, tr[i], &TS[i]);
        static prior_t P2; pr_build(&P2, tr, trl, m, (int)R.n_class);
        for (int i = 0; i < n; i++) {
            if (fold[i] != f) continue;
            if (!strcmp(R.names[R.label[i]], "none")) continue;   /* IoT only */
            tvec q; t_encode(&S, U_t[i], &q); int aa = t_active(&q);
            int best = -(1 << 28); uint32_t bi = 0;
            for (int k = 0; k < m; k++) { int s = t_score(&q, &TS[k], aa);
                if (s > best) { best = s; bi = k; } }
            int rc = r_apply_polarity(&S, trl[bi], U_t[i]);
            int mg; int pv = pr_vote(&P2, U_t[i], &mg);
            int rok = rc >= 0 && rc == (int)R.label[i];
            int pok = pv >= 0 && pv == (int)R.label[i];
            /* Does this eval item have a NEAR-DUPLICATE in its own training fold?
               The corpus is 36-37%% near-duplicates (leakchk), so a random split
               puts paraphrases across folds - and the WORD prior can memorise a
               paraphrase where the n-gram router cannot. If the prior advantage
               lives entirely in these items, index CV is invalid here. */
            int dup = 0;
            { char a[512]; r_norm(U_t[i], a, sizeof a);
              for (int k = 0; k < m && !dup; k++) {
                char b2[512]; r_norm(tr[k], b2, sizeof b2);
                /* word-overlap Dice, same measure leakchk uses */
                int inter = 0, na = 0, nb = 0;
                char *sa[48]; int cnta = 0; char ca[512]; strcpy(ca, a);
                for (char *t2 = strtok(ca, " "); t2 && cnta < 48; t2 = strtok(NULL, " ")) sa[cnta++] = t2;
                char cb[512]; strcpy(cb, b2); na = cnta;
                for (char *t2 = strtok(cb, " "); t2; t2 = strtok(NULL, " ")) {
                  nb++; for (int z = 0; z < cnta; z++) if (!strcmp(sa[z], t2)) { inter++; break; } }
                if (na + nb && 200 * inter / (na + nb) >= 80) dup = 1;
              } }
            printf("XV\t%d\t%d\t%d\t%d\t%d\n", mg, best, rok, pok, dup);
        }
        free(TS);
    }
}
/* Measure what a compact gate table actually needs. My synthesis claimed 96 KB
   from storing argmax+margin per bucket; that was WRONG — pr_vote sums clamped
   per-class lift deltas across words, so the per-class deltas are required. */
static void gatesize(void) {
    long live = 0, nz = 0, maxnz = 0;
    int dmin = 1 << 30, dmax = -(1 << 30);
    long hist[9] = {0};
    for (int h = 0; h < PR_HASH; h++) {
        int64_t tot = PR.w_tot[h];
        if (tot < 2) continue;                 /* pr_vote skips these entirely */
        live++;
        int k = 0;
        for (uint32_t c = 0; c < R.n_class; c++) {
            int64_t cnt = PR.w_cls[h][c];
            if (!cnt || !PR.cls_n[c]) continue;
            int64_t lift = (cnt * PR.n_total * PR_SCALE) / ((int64_t)PR.cls_n[c] * tot);
            int64_t d = lift - PR_SCALE;
            if (d >  4 * PR_SCALE) d =  4 * PR_SCALE;
            if (d < -PR_SCALE)     d = -PR_SCALE;
            if (d) { k++; if (d < dmin) dmin = (int)d; if (d > dmax) dmax = (int)d; }
        }
        nz += k; if (k > maxnz) maxnz = k;
        hist[k > 8 ? 8 : k]++;
    }
    printf("  buckets total          %d\n", PR_HASH);
    printf("  live (w_tot >= 2)      %ld  (%.1f%%)\n", live, 100.0*live/PR_HASH);
    printf("  nonzero deltas         %ld  (avg %.2f per live bucket, max %ld)\n",
           nz, (double)nz/live, maxnz);
    printf("  delta range            [%d, %d]  -> fits int16\n", dmin, dmax);
    printf("  nonzero-count histogram:");
    for (int i = 0; i <= 8; i++) printf(" %d:%ld", i, hist[i]);
    printf("\n\n  candidate encodings:\n");
    printf("    full prior_t                     %8.2f MB\n", sizeof(prior_t)/1048576.0);
    printf("    dense int16 deltas, all buckets   %8.2f MB\n", (double)PR_HASH*RMAXCLS*2/1048576.0);
    printf("    dense int16, live buckets only    %8.2f KB\n", (double)live*RMAXCLS*2/1024.0);
    printf("    sparse (cls:u8 + delta:i16) pairs %8.2f KB  + %.2f KB index\n",
           (double)nz*3/1024.0, (double)PR_HASH*4/1024.0);
}
/* --gatecheck: the compact table must be BIT-IDENTICAL to the full prior.
   Not "close enough" — every class and every margin, on every dev item. */
static void gatecheck(void) {
    static gate_t G;
    gate_build(&G, &PR);
    printf("  full prior_t   %8.2f MB\n", sizeof(prior_t)/1048576.0);
    printf("  gate table     %8.2f KB   (%u pairs)  -> %.0fx smaller\n",
           gate_bytes(&G)/1024.0, G.npair, (double)sizeof(prior_t)/gate_bytes(&G));
    int bad_c = 0, bad_m = 0, n = 0, abst = 0;
    for (int i = 0; i < V_n; i++) {
        int m1, m2;
        int a = pr_vote(&PR, V_t[i], &m1);
        int b = gate_vote(&G, V_t[i], &m2);
        n++; if (a < 0) abst++;
        if (a != b) bad_c++;
        if (m1 != m2) bad_m++;
    }
    for (int i = 0; i < U_n; i++) {          /* and every index utterance too */
        int m1, m2;
        int a = pr_vote(&PR, U_t[i], &m1);
        int b = gate_vote(&G, U_t[i], &m2);
        n++;
        if (a != b) bad_c++;
        if (m1 != m2) bad_m++;
    }
    printf("  checked %d utterances (%d dev + %d index), %d abstentions\n", n, V_n, U_n, abst);
    printf("  class mismatches %d   margin mismatches %d   %s\n",
           bad_c, bad_m, (!bad_c && !bad_m) ? "EXACT" : "*** DIVERGENT ***");
}
/* --selsig: was the DEV selector gain ever significant? Paired McNemar between
   baseline and selector on the same dev items. wa fell 13->10, three items.
   Three items was also the size of the held-out REGRESSION. */
static void pairsig(const char *name, hit (*fa)(const char *), hit (*fb)(const char *), int tha, int thb) {
    int b = 0, c = 0;
    for (int i = 0; i < V_n; i++) {
        hit h0 = fa(V_t[i]), h1 = fb(V_t[i]);
        int c0 = (h0.score > tha) ? h0.cls : -1, c1 = (h1.score > thb) ? h1.cls : -1;
        int ok0 = !strcmp(c0 < 0 ? "none" : R.names[c0], V_l[i]);
        int ok1 = !strcmp(c1 < 0 ? "none" : R.names[c1], V_l[i]);
        if (ok0 != ok1) { if (ok1) b++; else c++; }
    }
    int n = b + c; double s = 0, tot = 0;
    int lo = b < c ? b : c, hi = b > c ? b : c;
    for (int k = 0; k <= n; k++) {
        double ways = 1; for (int j = 0; j < k; j++) ways = ways * (n - j) / (j + 1);
        tot += ways; if (k <= lo || k >= hi) s += ways;
    }
    double p = n ? s / tot : 1.0;
    printf("  %-34s fixed %-4d broke %-4d  p=%.4f  %s\n", name, b, c, p,
           p < 0.05 ? "SIGNIFICANT" : "not significant");
}
static void selsig(void) {
    int b = 0, c = 0, n = 0;
    for (int i = 0; i < V_n; i++) {
        int save = PRIORCLS, sg = USEGATE;
        PRIORCLS = 0; USEGATE = 0;
        hit h0 = score_ter(V_t[i]);
        PRIORCLS = 1; USEGATE = 1;
        hit h1 = score_ter(V_t[i]);
        PRIORCLS = save; USEGATE = sg;
        int c0 = (h0.score > 136) ? h0.cls : -1;
        int c1 = (h1.score > 136) ? h1.cls : -1;
        const char *p0 = c0 < 0 ? "none" : R.names[c0];
        const char *p1 = c1 < 0 ? "none" : R.names[c1];
        int ok0 = !strcmp(p0, V_l[i]), ok1 = !strcmp(p1, V_l[i]);
        if (ok0 != ok1) { n++; if (ok1) b++; else c++; }
    }
    printf("\n  === dev: paired baseline vs selector ===\n");
    printf("    selector fixed   %d\n", b);
    printf("    selector broke   %d\n", c);
    printf("    discordant       %d\n", n);
    double p = 1.0; if (n > 0) {              /* exact two-sided binomial */
        double s = 0, tot = 0;
        for (int k = 0; k <= n; k++) {
            double ways = 1; for (int j = 0; j < k; j++) ways = ways * (n - j) / (j + 1);
            tot += ways; if (k <= (b < c ? b : c) || k >= (b > c ? b : c)) s += ways;
        }
        p = s / tot;
    }
    printf("    exact two-sided p = %.3f   %s\n", p,
           p < 0.05 ? "significant" : "NOT significant");
    printf("\n  === retroactive: apply the same test to accepted claims ===\n");
    pairsig("twin-ternary vs binary (d=256)", score_bin, score_ter, 138, 136);
}
/* --route / --repl: try the router on one sentence and SEE WHY.
 * For a nearest-neighbour system the honest explanation is the neighbours, so
 * this shows them. Until now the repo could only report aggregate statistics
 * over a corpus — there was no way to simply talk to the thing it builds. */
static void route_one(const char *txt) {
    tvec q; t_encode(&R, txt, &q); int aa = t_active(&q);
    int th = (FIXTH != (1<<30)) ? FIXTH : RSHIP_TH;
    int bi[5]; int bs[5]; for (int k = 0; k < 5; k++) { bi[k] = -1; bs[k] = -(1<<28); }
    for (uint32_t i = 0; i < R.n_index; i++) {
        int s = t_score(&q, &TI[i], aa);
        for (int k = 0; k < 5; k++)
            if (s > bs[k]) { for (int j = 4; j > k; j--) { bs[j]=bs[j-1]; bi[j]=bi[j-1]; }
                             bs[k]=s; bi[k]=(int)i; break; }
    }
    int raw = R.label[bi[0]];
    int cls = r_apply_polarity(&R, raw, txt);
    int pol = r_polarity(txt);
    int act = bs[0] > th;
    printf("\n  \"%s\"\n", txt);
    int win_is_none = !strcmp(R.names[cls], "none");
    printf("    %-22s %s\n", "decision",
           !act        ? "none  (score below threshold — no action)"
           : win_is_none ? "none  (nearest match is not a command — no action)"
                         : R.names[cls]);
    printf("    %-22s %d   (threshold %d, margin %+d)\n", "score", bs[0], th, bs[0]-th);
    if (pol && cls != raw)
        printf("    %-22s %s cue moved %s -> %s\n", "polarity",
               pol > 0 ? "positive" : "negative", R.names[raw], R.names[cls]);
    else if (pol)
        printf("    %-22s %s cue present, winner already agrees\n", "polarity",
               pol > 0 ? "positive" : "negative");
    printf("    %-22s %d of %d dims carry evidence\n", "encoding", aa, RD);
    printf("    nearest stored utterances:\n");
    for (int k = 0; k < 5 && bi[k] >= 0; k++)
        printf("      %4d  %-22s \"%s\"\n", bs[k], R.names[R.label[bi[k]]], U_t[bi[k]]);
}
static void repl(void) {
    char line[512];
    printf("\n  mogwai — type an utterance, blank line or Ctrl-D to exit\n");
    printf("  threshold %d, index %u vectors\n", (FIXTH != (1<<30)) ? FIXTH : RSHIP_TH, R.n_index);
    for (;;) {
        printf("\n  > "); fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) break;
        size_t n = strlen(line);
        while (n && (line[n-1]=='\n' || line[n-1]=='\r')) line[--n] = 0;
        if (!n) break;
        route_one(line);
    }
    printf("\n");
}
static void usage(void) {
    printf(
"mogwai compare — twin-ternary intent router harness\n"
"\n"
"  compare [paths] [flags]      paths default to data/ ; run `make fetch` first\n"
"\n"
"TRY IT\n"
"  --route=\"turn off the kitchen light\"   route one utterance, show why\n"
"  --repl                                  same, interactively\n"
"\n"
"MEASURE  (dev/validation — safe to run as often as you like)\n"
"  --ship                 the SHIPPED operating point (threshold %d).\n"
"                         Reproduces the table in README.md.\n"
"  --fixth=N              force a threshold instead of tuning one\n"
"  --curve                sweep the threshold, emit the operating curve\n"
"  --errs                 print the actual misclassified utterances\n"
"\n"
"HELD-OUT  (burns one unit of results/TEST_BUDGET — see doc/METHOD.md)\n"
"  --test                 evaluate on the test split. Prefer `make testset`.\n"
"\n"
"INDEX PRUNING\n"
"  --prune-dup            drop identical codes (measured: zero exist at d=256)\n"
"  --prune-cnn            drop negatives that are nobody's nearest neighbour\n"
"  --prune-neg=K          keep 1-in-K negatives\n"
"\n"
"RETAINED NEGATIVE RESULTS  (kept per `never delete`; none of these help —\n"
"each is reproducible evidence of a measured failure, see EXPERIMENTS.md)\n"
"  --cue                  index-derived hard word cues .......... harmful\n"
"  --priorcls[2]          word prior picks the class ............ harmful\n"
"  --gatesel --selmargin=N  margin-gated selector ............... failed held-out\n"
"  --sig=N --noveto       signature veto ....................... inert\n"
"\n"
"DIAGNOSTICS\n"
"  --channels             router vs word-prior error overlap\n"
"  --xval                 2-fold cross-validation inside the index\n"
"  --gatecheck            gate table must equal the prior bit-exactly\n"
"  --gatesize             what a compact prior table would need\n"
"  --density              index vector sparsity — the footprint lever\n"
"  --selsig               paired significance of the selector on dev\n"
"  --seldump --dirdump    per-item signal dumps (TSV)\n"
"  --leak                 reintroduce the 75.6%% leak on purpose, to prove the guard\n"
"\n"
"See also: make ship | make regress | make testset | doc/TOOLS.md\n", RSHIP_TH);
}
static void density(void) {
    long tot = 0, mn = 1<<30, mx = 0; long hist[9] = {0};
    for (int i = 0; i < U_n; i++) {
        int a = t_active(&TI[i]);
        tot += a; if (a < mn) mn = a; if (a > mx) mx = a;
        hist[a * 8 / (RD + 1)]++;
    }
    double avg = (double)tot / U_n;
    printf("\n  === index vector density (footprint lever, since we are 92%% byte-bound) ===\n");
    printf("    active dims per vector: mean %.1f of %d (%.1f%%), min %ld, max %ld\n",
           avg, RD, 100.0*avg/RD, mn, mx);
    printf("    octile histogram:");
    for (int i = 0; i < 8; i++) printf(" %ld", hist[i]);
    printf("\n\n    current bit-plane layout : %d B/vector  (2 bits/dim, TRIX density)\n", (int)sizeof(tvec));
    printf("    sparse (u8 idx + sign)   : %.0f B/vector average  -> %.2fx %s\n",
           avg * 1.125, sizeof(tvec) / (avg * 1.125),
           avg * 1.125 < sizeof(tvec) ? "SMALLER" : "LARGER");
    printf("    but: sparse needs a gather per dim, and popcount over bit-planes is\n");
    printf("    branchless. The question is whether fewer BYTES beats more CYCLES,\n");
    printf("    and at 92%% byte-bound the answer may be yes.\n");
}
static void report(const char *name, hit (*f)(const char *), int lo, int hi, double kb) {
    hit *hv = precompute(f, V_t, V_n);
    int th = (FIXTH != (1<<30)) ? FIXTH : tune(hv, V_l, V_n, lo, hi);
    hit *hh = USE_TEST ? precompute(f, T_t, T_n) : hv;
    char (*lab)[RNAMELEN] = USE_TEST ? T_l : V_l;
    int n = USE_TEST ? T_n : V_n;
    TX z = tally(hh, th, lab, n);
    if(DUMPERR && !USE_TEST) dump_errs(hh, th, lab, V_t, n);
    if(DIRDUMP && !USE_TEST) dirdump(hh, lab, n);
    double p = z.in ? (double)z.iok / z.in : 0.0;
    double se = 100.0 * sqrt(p * (1 - p) / (z.in ? z.in : 1));
    printf("  %-20s %6.1f%% +-%.1f  %-7d %-7d %7.0f  th=%d\n",
           name, 100.0 * p, se, z.fa + z.wa, z.ms, kb, th);
    /* Machine-readable row. The Makefile used to scrape the line above with
       awk on whitespace fields, which silently corrupted every log entry:
       "binary (1 bit)" is THREE fields, so the variant logged as "binary (1"
       and every later column shifted. index_kb was lost entirely. The writer
       emits its own row now; nothing parses formatted output. */
    printf("ROW\t%s\t%s\t%.1f\t%.1f\t%d\t%d\t%d\t%.0f\t%d\t%d\n",
           USE_TEST ? "TEST" : "dev", name, 100.0 * p, se,
           z.fa, z.wa, z.ms, kb, th, n);
    if (LAST) mcnemar(LAST, LAST_TH, hh, th, lab, n);
    LAST = hh; LAST_TH = th; LAST_NAME = name;
}
int main(int argc,char**argv){
    /* Flags may appear anywhere; the four corpus paths are optional and default
       to data/. Unknown flags are REFUSED — they used to be silently ignored,
       so `--shipp` ran unshipped and reported the wrong numbers with no warning. */
    const char *pos[4] = {0,0,0,0}; int np = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-') { if (np < 4) pos[np++] = a; else { fprintf(stderr,"  too many paths: %s\n",a); return 1; } continue; }
        if      (!strcmp(a,"-h") || !strcmp(a,"--help")) { usage(); return 0; }
        else if (!strncmp(a,"--route=",8)) { ROUTE1 = a+8; }
        else if (!strcmp(a,"--repl")) REPL=1;
        else if (!strcmp(a,"--test")) USE_TEST=1;
        else if (!strncmp(a,"--sig=",6)) SIGMODE=atoi(a+6);
        else if (!strcmp(a,"--noveto")) NOVETO=1;
        else if (!strcmp(a,"--leak")) LEAKTEST=1;
        else if (!strncmp(a,"--fixth=",8)) FIXTH=atoi(a+8);
        else if (!strcmp(a,"--curve")) CURVE=1;
        else if (!strcmp(a,"--errs")) DUMPERR=1;
        else if (!strcmp(a,"--dirdump")) DIRDUMP=1;
        else if (!strcmp(a,"--cue")) USECUE=1;
        else if (!strcmp(a,"--channels")) CHANNELS=1;
        else if (!strcmp(a,"--seldump")) SELDUMP=1;
        else if (!strcmp(a,"--xval")) XVAL=1;
        else if (!strcmp(a,"--gatesize")) GATESZ=1;
        else if (!strcmp(a,"--density")) DENSITY=1;
        else if (!strcmp(a,"--gatecheck")) GATECHK=1;
        else if (!strcmp(a,"--gatesel")) { USEGATE=1; PRIORCLS=1; }
        else if (!strcmp(a,"--selsig")) { SELSIG=1; SELMARG=8; }
        else if (!strcmp(a,"--priorcls")) PRIORCLS=1;
        else if (!strcmp(a,"--priorcls2")) PRIORCLS=2;
        else if (!strncmp(a,"--selmargin=",12)) SELMARG=atoi(a+12);
        else if (!strcmp(a,"--ship")) FIXTH=RSHIP_TH;
        else if (prune_parse(a,&PRUNE)) { /* consumed */ }
        else { fprintf(stderr,"  unknown flag: %s\n  try --help\n", a); return 1; }
    }
    if (np == 0) {                      /* the common case: just use data/ */
        pos[0]="data/train.json"; pos[1]="data/validation.json";
        pos[2]="data/test.json";  pos[3]="data/nlu_home.csv"; np=4;
    }
    if (np != 4) { fprintf(stderr,"  need 4 corpus paths or none (defaults to data/)\n  try --help\n"); return 1; }
    { FILE *probe = fopen(pos[0], "r");
      if (!probe) { fprintf(stderr,
          "  cannot open %s\n"
          "  the corpora are fetched, not vendored — run:  make fetch\n", pos[0]); return 1; }
      fclose(probe); }
    argv[1]=(char*)pos[0]; argv[2]=(char*)pos[1]; argv[3]=(char*)pos[2]; argv[4]=(char*)pos[3];
    if(ROUTE1 || REPL) INV_QUIET = 1;
    if(USE_TEST){
        /* The counter used to live in results/TEST_BUDGET alongside the prose
           audit note. fscanf("%d") cannot parse a file starting with text, so it
           silently read 0, incremented to 1, and truncated the file — destroying
           the annotation and resetting the count on EVERY use. A guard that
           cannot count past one is not a guard. Counter and log are now split:
           the count is machine-readable, the log is append-only. */
        int used=0; FILE *bf=fopen("results/TEST_BUDGET_COUNT","r");
        if(bf){ if(fscanf(bf,"%d",&used)!=1) used=0; fclose(bf); }
        used++;
        bf=fopen("results/TEST_BUDGET_COUNT","w");
        if(bf){ fprintf(bf,"%d\n",used); fclose(bf); }
        bf=fopen("results/TEST_BUDGET","a");        /* append, never truncate */
        if(bf){ time_t now=time(NULL); char ts[32];
            strftime(ts,sizeof ts,"%Y-%m-%dT%H:%M:%SZ",gmtime(&now));
            fprintf(bf,"evaluation %d: %s  argv:",used,ts);
            for(int k=1;k<argc;k++) fprintf(bf," %s",argv[k]);
            fprintf(bf,"\n"); fclose(bf); }
        fprintf(stderr,"  *** TEST SET TOUCHED (use #%d). Every touch is a chance to overfit. ***\n",used);
    } else if(!ROUTE1 && !REPL) fprintf(stderr,"  reporting on VALIDATION (pass --test to evaluate on test)\n");
    char line[8192],t[512],l[RNAMELEN];
    FILE*f;
    /* TEST is loaded FIRST so the index can exclude any string that
       also appears in test — MASSIVE train and test share utterances. */
    f=fopen(argv[3],"r");
    while(fgets(line,sizeof line,f)) if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l))
        { push(T_t,T_l,&T_n,t,isiot(l)?l:"none"); hs_add(t); }
    fclose(f);

    /* DEV comes out of TRAIN: validation has only 118 IoT, so a dev set carved
       from it is n=59 and cannot resolve anything. Train has 769. */
    f=fopen(argv[1],"r"); int ti=0, tn=0;
    while(fgets(line,sizeof line,f)) if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l)){
        int io=isiot(l);
        if(hs_has(t)) continue;                 /* MASSIVE train repeats strings */
        if(io && (ti++ % 4)==0){ push(V_t,V_l,&V_n,t,l); if(!LEAKTEST) hs_add(t); continue; }
        if(!io && (tn++ % 8)==0){ push(V_t,V_l,&V_n,t,"none"); hs_add(t); continue; }
        push(U_t,U_l,&U_n,t,io?l:"none"); hs_add(t);
    }
    fclose(f); int n_train=U_n;
    f=fopen(argv[2],"r");
    while(fgets(line,sizeof line,f)) if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l))
        if(isiot(l)){ push(U_t,U_l,&U_n,t,l); hs_add(t); }   /* all val IoT -> index */

    f=fopen(argv[4],"r"); int added=0,dup=0;
    while(fgets(line,sizeof line,f)){
        char*fl[12]={0}; int nf=0,inq=0; char*p=line; fl[nf++]=p;
        for(;*p&&nf<12;p++){ if(*p=='"')inq=!inq; else if(*p==';'&&!inq){*p=0;fl[nf++]=p+1;} }
        if(nf<10)continue;
        for(int i=0;i<nf;i++){char*s=fl[i];int L=(int)strlen(s);
            while(L&&(s[L-1]=='\n'||s[L-1]=='\r'))s[--L]=0;
            if(L>=2&&s[0]=='"'&&s[L-1]=='"'){s[L-1]=0;fl[i]=s+1;} }
        if(strcmp(fl[2],"iot"))continue;
        if(hs_has(fl[9])){dup++;continue;}
        char lb[RNAMELEN]; snprintf(lb,sizeof lb,"iot_%s",fl[3]);
        push(U_t,U_l,&U_n,fl[9],lb); hs_add(fl[9]); added++;
    } fclose(f);
    if(!ROUTE1 && !REPL) fprintf(stderr,"  index %d (train %d, +NLU %d new, %d dedup) | DEV %d | test %d\n",
            U_n,n_train,added,dup,V_n,T_n);
    inv_disjoint("index vs DEV",  U_t, U_n, V_t, V_n);
    inv_disjoint("index vs TEST", U_t, U_n, T_t, T_n);
    memset(&R,0,sizeof R); R.magic=RMAGIC; R.dim=RD; R.n_index=U_n;
    for(int i=0;i<U_n;i++){ int fnd=-1;
        for(uint32_t c=0;c<R.n_class;c++) if(!strcmp(R.names[c],U_l[i])){fnd=c;break;}
        if(fnd<0&&R.n_class<RMAXCLS) snprintf(R.names[R.n_class++],RNAMELEN,"%.*s",RNAMELEN-1,U_l[i]); }
    int64_t sum[RD]; memset(sum,0,sizeof sum); int16_t acc[RD]; int32_t tot;
    for(int i=0;i<U_n;i++){ r_counts(U_t[i],acc,&tot);
        for(int d=0;d<RD;d++) sum[d]+=((int64_t)acc[d]*RSCALE)/tot; }
    for(int d=0;d<RD;d++) R.centre[d]=(int32_t)(sum[d]/U_n);
    R.index=calloc(U_n,sizeof(rvec)); R.label=calloc(U_n,1); TI=calloc(U_n,sizeof(tvec));
    for(int i=0;i<U_n;i++){ r_encode(&R,U_t[i],&R.index[i]); t_encode(&R,U_t[i],&TI[i]);
        for(uint32_t c=0;c<R.n_class;c++) if(!strcmp(R.names[c],U_l[i])){R.label[i]=c;break;} }
    /* Pruning runs AFTER encoding with the full-index centre, so every surviving
       code is bit-identical to the unpruned run: this isolates "fewer stored
       vectors" from "different encoder". Shared with mkblob (see prune.h). */
    U_n = prune_index(U_t, U_l, &R, TI, R.index, U_n, PRUNE, 1);
    static int base[RD]; memset(base,0,sizeof base);
    for(int i=0;i<U_n;i++) for(int d=0;d<RD;d++)
        if(TI[i].m[d>>5]&(1u<<(d&31))) base[d]++;
    for(uint32_t c=0;c<R.n_class;c++){
        int cnt=0; static int ob[RD],om[RD]; memset(ob,0,sizeof ob); memset(om,0,sizeof om);
        for(int i=0;i<U_n;i++) if(R.label[i]==c){ cnt++;
            for(int d=0;d<RD;d++){ if(R.index[i].w[d>>5]&(1u<<(d&31))) ob[d]++;
                                   if(TI[i].m[d>>5]&(1u<<(d&31))) om[d]++; } }
        for(int d=0;d<RD;d++){
            if(cnt&&ob[d]*2>=cnt) R.sig[c].w[d>>5]|=1u<<(d&31);
            if(cnt&&om[d]*4>=cnt){ TSIG[c].m[d>>5]|=1u<<(d&31);
                if(ob[d]*2>=om[d]) TSIG[c].s[d>>5]|=1u<<(d&31); } } }
    pr_build(&PR, U_t, R.label, U_n, (int)R.n_class);
    cue_build(&CUE, U_t, R.label, U_n, (int)R.n_class);   /* index only — never dev */
    gate_build(&GATE, &PR);   /* compact form of PR, verified bit-exact */
    long tb=0; for(int i=0;i<U_n;i++) tb+=strlen(U_t[i])+1;
    /* route/repl produce only the route — no corpus diagnostics, no table */
    if(ROUTE1){ route_one(ROUTE1); printf("\n"); return 0; }
    if(REPL){ repl(); return 0; }
    code_overlap("DEV", V_t, V_l, V_n);
    code_overlap("TEST", T_t, T_l, T_n);
    printf("\n  %-20s %8s %-8s %-8s %8s\n","representation","iot acc","wrong","missed","index KB");
    if(CURVE){ curve("binary",score_bin,-RD,RD);
               curve("no-prior",score_ter,-512,512); curve("prior",score_ter_wp,-512,512); return 0; }
    if(CHANNELS){ channels(); return 0; }
    if(SELDUMP){ seldump(); return 0; }
    if(XVAL){ xval(); return 0; }
    if(GATESZ){ gatesize(); return 0; }
    if(DENSITY){ density(); return 0; }
    if(GATECHK){ gatecheck(); return 0; }
    if(SELSIG){ selsig(); return 0; }
    report("binary (1 bit)",   score_bin,-RD,RD,          U_n*sizeof(rvec)/1024.0);
    report("twin-ternary (2b)",score_ter,-512,512,        U_n*sizeof(tvec)/1024.0);
    /* word prior CUT: passes breaks-zero but does not move the operating curve.
       Reproduce with --curve. Code retained in prior.c, not in the router path. */
    (void)score_cas; (void)tb;   /* cascade measured: identical on every axis, cut */
    return 0;
}
