/* compare.c — binary vs twin-ternary vs cascade. Integer only.
 * Each router scores a query ONCE; thresholds sweep cached scores. */
#include "ternary.h"
#include "cascade.h"
#include "invariants.h"
#include "prior.h"
#include "prune.h"
#include "cue.h"
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
static int PRIORCLS = 0;  /* --priorcls: router accepts/rejects, prior picks the class */

static int js(const char*l,const char*k,char*o,int cap){
    char pat[64]; snprintf(pat,sizeof pat,"\"%s\":",k);
    const char*p=strstr(l,pat); if(!p)return 0; p+=strlen(pat);
    while(*p==' ')p++; if(*p!='"')return 0; p++;
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
        if(pv>=0 && cls0>=0 && strcmp(R.names[cls0],"none")
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
    printf("      vs %-18s wrong: fixed %-3d broke %-3d %s\n",
           LAST_NAME, fixed, broke, broke == 0 ? "(non-destructive)" : "");
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
static void code_overlap(const char *tag, char **X, char lx[][RNAMELEN], int nx) {
    enum { HB = 1 << 15 };
    static int head[HB], nxt[40000]; static int built = 0;
    if (!built) {
        for (int i = 0; i < HB; i++) head[i] = -1;
        for (int i = 0; i < U_n; i++) {
            const unsigned char *b = (const unsigned char *)&TI[i];
            uint32_t h = 2166136261u;
            for (size_t k = 0; k < sizeof(tvec); k++) { h ^= b[k]; h *= 16777619u; }
            uint32_t s = h & (HB - 1); nxt[i] = head[s]; head[s] = i;
        }
        built = 1;
    }
    int iot = 0, coll = 0, coll_iot = 0;
    for (int i = 0; i < nx; i++) {
        int is_iot = strcmp(lx[i], "none") != 0;
        iot += is_iot;
        tvec q; t_encode(&R, X[i], &q);
        const unsigned char *b = (const unsigned char *)&q;
        uint32_t h = 2166136261u;
        for (size_t k = 0; k < sizeof(tvec); k++) { h ^= b[k]; h *= 16777619u; }
        for (int j = head[h & (HB - 1)]; j >= 0; j = nxt[j])
            if (!memcmp(&TI[j], &q, sizeof(tvec))) { coll++; coll_iot += is_iot; break; }
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
    if(argc<5){fprintf(stderr,"usage: compare train val test nlu.csv [--test]\n");return 1;}
    for(int i=5;i<argc;i++){ if(!strcmp(argv[i],"--test")) USE_TEST=1;
        else if(!strncmp(argv[i],"--sig=",6)) SIGMODE=atoi(argv[i]+6);
        else if(!strcmp(argv[i],"--noveto")) NOVETO=1;
        else if(!strcmp(argv[i],"--leak")) LEAKTEST=1;
        else if(!strncmp(argv[i],"--fixth=",8)) FIXTH=atoi(argv[i]+8);
        else if(!strcmp(argv[i],"--curve")) CURVE=1;
        else if(!strcmp(argv[i],"--errs")) DUMPERR=1;
        else if(!strcmp(argv[i],"--dirdump")) DIRDUMP=1;
        else if(!strcmp(argv[i],"--cue")) USECUE=1;
        else if(!strcmp(argv[i],"--channels")) CHANNELS=1;
        else if(!strcmp(argv[i],"--priorcls")) PRIORCLS=1;
        else if(!strcmp(argv[i],"--priorcls2")) PRIORCLS=2;
        else if(!strcmp(argv[i],"--ship")) FIXTH=RSHIP_TH;
        else if(prune_parse(argv[i],&PRUNE)) { /* consumed */ }
}
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
    } else fprintf(stderr,"  reporting on VALIDATION (pass --test to evaluate on test)\n");
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
    fprintf(stderr,"  index %d (train %d, +NLU %d new, %d dedup) | DEV %d | test %d\n",
            U_n,n_train,added,dup,V_n,T_n);
    inv_disjoint("index vs DEV",  U_t, U_n, V_t, V_n);
    inv_disjoint("index vs TEST", U_t, U_n, T_t, T_n);
    memset(&R,0,sizeof R); R.magic=RMAGIC; R.dim=RD; R.n_index=U_n;
    for(int i=0;i<U_n;i++){ int fnd=-1;
        for(uint32_t c=0;c<R.n_class;c++) if(!strcmp(R.names[c],U_l[i])){fnd=c;break;}
        if(fnd<0&&R.n_class<RMAXCLS) snprintf(R.names[R.n_class++],RNAMELEN,"%s",U_l[i]); }
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
    long tb=0; for(int i=0;i<U_n;i++) tb+=strlen(U_t[i])+1;
    code_overlap("DEV", V_t, V_l, V_n);
    code_overlap("TEST", T_t, T_l, T_n);
    printf("\n  %-20s %8s %-8s %-8s %8s\n","representation","iot acc","wrong","missed","index KB");
    if(CURVE){ curve("binary",score_bin,-RD,RD);
               curve("no-prior",score_ter,-512,512); curve("prior",score_ter_wp,-512,512); return 0; }
    if(CHANNELS){ channels(); return 0; }
    report("binary (1 bit)",   score_bin,-RD,RD,          U_n*sizeof(rvec)/1024.0);
    report("twin-ternary (2b)",score_ter,-512,512,        U_n*sizeof(tvec)/1024.0);
    /* word prior CUT: passes breaks-zero but does not move the operating curve.
       Reproduce with --curve. Code retained in prior.c, not in the router path. */
    (void)score_cas; (void)tb;   /* cascade measured: identical on every axis, cut */
    return 0;
}
