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
static int USE_TEST = 0;
static prune_opt PRUNE = {0,0,0,0,0,0};
/* METHOD 19 enforcement; defined below, used by every decision-policy experiment */
static void control_or_die(const char *what, int fa, int wa, int ms, int ok, int th);
static cue_t CUE; static int USECUE = 0;   /* --cue: index-derived hard word cues */
static int CHANNELS = 0;   /* --channels: word vs n-gram error overlap */
static int SELDUMP = 0;   /* --seldump: per-item signals a selector could use */
static int XVAL = 0;      /* --xval: 2-fold CV inside the index */
static int GATESZ = 0;    /* --gatesize: what a compact prior table needs */
static int DENSITY = 0;   /* --density: how sparse are the index vectors? */
static int FOOTPRINT = 0; /* --footprint: bytes, and what each encoding would cost */
static int LMMRAW = 0;    /* --lmm-raw: footprint observations */
static int LMMRAW3 = 0;   /* --lmm-raw3: what rejection needs */
static int ASYM = 0;      /* --asym: exact positives + statistical negatives */
static int GATECHK = 0;   /* --gatecheck: gate must equal prior bit-exactly */
static gate_t GATE; static int USEGATE = 0;  /* --gatesel: selector reads the compact table */
static int SELSIG = 0;    /* --selsig: paired significance of the dev selector gain */
static const char *ROUTE1 = 0;  /* --route="..." */
static int PRUNEDIAG = 0;       /* --prune-diag: guard-coverage table, internal */
static int REPL = 0;            /* --repl */
static int PRIORCLS = 0;  /* --priorcls: router accepts/rejects, prior picks the class */
static int SELMARG = 0;   /* --selmargin=N: prior votes only when its margin >= N */
static int PACKBENCH = 0; /* --packbench: packed sign plane, cycles vs bytes */

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

/* ---- rank oracle -------------------------------------------------------
 * Before building a second tier, find out whether a second tier COULD help.
 * For every dev error, where does the right answer sit in the current coarse
 * ranking? If the correct class is at rank 37, no rerank over a shortlist
 * saves it and the direction is dead. If it is at rank 2-4, the information
 * survived the coarse scan and only the ORDERING is wrong.
 *
 * This is an upper bound on rerankability, not a result. The channel selector
 * had an oracle of 94.3% and still failed held-out (eval #4). */
static int RANKORACLE = 0;
static int RO_S[MAXU];

static void rankoracle(void) {
    int th = FIXTH >= 0 ? FIXTH : RSHIP_TH;
    const int Ks[7] = {1, 2, 4, 8, 16, 32, 64};
    int hist[3][8]; memset(hist, 0, sizeof hist);
    int tot[3] = {0,0,0};
    const char *nm[3] = {"wrong-act", "missed", "UNBIDDEN"};

    printf("\n  rank oracle at th=%d over %u index vectors\n", th, (unsigned)R.n_index);
    printf("  for each error: rank of the best exemplar that would give the RIGHT answer\n");
    printf("  (wrong-act/missed -> an exemplar of the true class; UNBIDDEN -> any negative)\n\n");

    for (int i = 0; i < V_n; i++) {
        const char *txt = V_t[i], *truth = V_l[i];
        tvec q; t_encode(&R, txt, &q); int aa = t_active(&q);
        int best = -(1 << 28); uint32_t bi = 0;
        for (uint32_t j = 0; j < R.n_index; j++) {
            RO_S[j] = t_score(&q, &TI[j], aa);
            if (RO_S[j] > best) { best = RO_S[j]; bi = j; }
        }
        int c = (best > th) ? r_apply_polarity(&R, R.label[bi], txt) : -1;
        const char *p = c < 0 ? "none" : R.names[c];
        if (!strcmp(p, truth)) continue;

        int gn = !strcmp(truth, "none");
        int kind = gn ? 2 : (!strcmp(p, "none") ? 1 : 0);
        tot[kind]++;

        int tbest = -(1 << 28);
        for (uint32_t j = 0; j < R.n_index; j++) {
            const char *jn = gn ? R.names[R.label[j]]
                                : R.names[r_apply_polarity(&R, R.label[j], txt)];
            if (strcmp(jn, truth)) continue;
            if (RO_S[j] > tbest) tbest = RO_S[j];
        }
        int rank = -1;
        if (tbest > -(1 << 28)) {
            rank = 1;
            for (uint32_t j = 0; j < R.n_index; j++) if (RO_S[j] > tbest) rank++;
        }

        if (rank > 0) { int b = 0; while (b < 6 && Ks[b] < rank) b++;
                        hist[kind][Ks[b] >= rank ? b : 7]++; }
        else hist[kind][7]++;

        if (rank > 0)
            printf("  %-9s rank=%-5d gap=%-4d  said=%-20s truth=%-20s \"%s\"\n",
                   nm[kind], rank, best - tbest, p, truth, txt);
        else
            printf("  %-9s rank=NONE          said=%-20s truth=%-20s \"%s\"\n",
                   nm[kind], p, truth, txt);
    }

    printf("\n  cumulative errors whose correct answer is within top-K\n");
    printf("  %-10s %6s %6s %6s %6s %6s %6s %6s %8s %6s\n",
           "", "K=1", "K=2", "K=4", "K=8", "K=16", "K=32", "K=64", ">64/none", "total");
    for (int t = 0; t < 3; t++) {
        printf("  %-10s", nm[t]);
        int cum = 0;
        for (int b = 0; b < 7; b++) { cum += hist[t][b]; printf(" %6d", cum); }
        printf(" %8d %6d\n", hist[t][7], tot[t]);
    }
}


/* ---- rerank oracle -----------------------------------------------------
 * Oracle #1 said the right answer is usually present in the coarse top-K and
 * merely mis-ordered. This asks whether the information to reorder it is
 * ALREADY IN HAND — i.e. whether t_encode is destroying it.
 *
 * t_encode reduces int16 acc[i] to one trit against a per-dim centre:
 *     sign_i = acc[i]*RSCALE >= centre[i]*total
 * The residual it throws away is the distance from that decision surface:
 *     delta_i = acc[i]*RSCALE - centre[i]*total
 * which is "how confidently did this dimension fire", relative to the
 * dimension's own expected rate. Not raw count — count is length-dependent and
 * the centre already normalises for it.
 *
 * Fine score, integer, same Dice denominator as the coarse metric so length is
 * handled identically:
 *     sum over dims active in BOTH of  qd_i * cd_i,  scaled by 2*RD/(aa+ab+T)
 * with each vector's deltas quantised to B bits against its own max |delta| —
 * a per-vector adaptive scale, as MTF7 uses.
 *
 * ORDERING ONLY. Accept/reject still uses the coarse best against the
 * threshold, so `missed` cannot move and any change in fa/wa is attributable
 * to reordering alone. fa can still improve: promoting a negative to rank 1
 * turns the answer into "none", which is a reject. */
static int CONDCENTRE = 0;
static int RERANK = 0;
/* same smoothing the shipped Dice uses; ternary.c takes it as a -D, so it is
   restated here rather than reached into. Measured flat over 2-16 at d=256. */
#define RR_TSMOOTH 8
#define RR_K 16
static int32_t *RR_D = NULL;      /* [n_index][RD] residuals */
static int32_t *RR_MX = NULL;     /* per-vector max |delta| over active dims */
static int32_t *RR_AB = NULL;     /* per-vector active count */

static void rr_delta(const char *txt, int32_t *d, int32_t *mx, int32_t *ab) {
    int16_t acc[RD]; int32_t total;
    r_counts(txt, acc, &total);
    int32_t m = 1, a = 0;
    for (int i = 0; i < RD; i++) {
        if (!acc[i]) { d[i] = 0; continue; }
        a++;
        d[i] = (int32_t)acc[i] * RSCALE - R.centre[i] * total;
        int32_t v = d[i] < 0 ? -d[i] : d[i];
        if (v > m) m = v;
    }
    *mx = m; *ab = a;
}
/* quantise to B bits signed: [-(2^(B-1)-1), +(2^(B-1)-1)]. B=0 -> full int32. */
static inline int32_t rr_q(int32_t v, int32_t mx, int B) {
    if (!B) return v;
    int32_t lim = (1 << (B - 1)) - 1;
    return (int32_t)(((int64_t)v * lim) / mx);
}

static void rerankoracle(void) {
    int th = FIXTH >= 0 ? FIXTH : RSHIP_TH;
    uint32_t n = R.n_index;
    RR_D  = malloc((size_t)n * RD * sizeof(int32_t));
    RR_MX = malloc((size_t)n * sizeof(int32_t));
    RR_AB = malloc((size_t)n * sizeof(int32_t));
    if (!RR_D || !RR_MX || !RR_AB) { printf("  out of memory\n"); return; }
    for (uint32_t j = 0; j < n; j++) rr_delta(U_t[j], RR_D + (size_t)j * RD, &RR_MX[j], &RR_AB[j]);

    /* one coarse scan per query; keep top-RR_K and the coarse winner */
    int  (*top)[RR_K] = malloc((size_t)V_n * sizeof *top);
    int  *cbest = malloc((size_t)V_n * sizeof(int));
    int  *cbi   = malloc((size_t)V_n * sizeof(int));
    int32_t *qd = malloc((size_t)V_n * RD * sizeof(int32_t));
    int32_t *qmx = malloc((size_t)V_n * sizeof(int32_t)), *qab = malloc((size_t)V_n * sizeof(int32_t));
    for (int i = 0; i < V_n; i++) {
        tvec q; t_encode(&R, V_t[i], &q); int aa = t_active(&q);
        int ts[RR_K], ti[RR_K];
        for (int k = 0; k < RR_K; k++) { ts[k] = -(1 << 28); ti[k] = 0; }
        for (uint32_t j = 0; j < n; j++) {
            int s = t_score(&q, &TI[j], aa);
            if (s <= ts[RR_K - 1]) continue;
            int k = RR_K - 1;
            while (k > 0 && ts[k - 1] < s) { ts[k] = ts[k - 1]; ti[k] = ti[k - 1]; k--; }
            ts[k] = s; ti[k] = (int)j;
        }
        cbest[i] = ts[0]; cbi[i] = ti[0];
        for (int k = 0; k < RR_K; k++) top[i][k] = ti[k];
        rr_delta(V_t[i], qd + (size_t)i * RD, &qmx[i], &qab[i]);
    }

    {   /* Is the residual even informative? If delta is nearly constant across
           active dims, it encodes "fired" - which the mask already has - and a
           fine metric built on it adds noise, not signal. Check before
           concluding anything about the information itself. */
        long nact=0, npos=0; long long amin=1LL<<60, amax=-(1LL<<60), asum=0;
        for (uint32_t j = 0; j < n; j++) {
            const int32_t *d = RR_D + (size_t)j * RD;
            for (int t = 0; t < RD; t++) {
                if (!d[t]) continue;
                nact++;
                if (d[t] > 0) npos++;
                long long v = d[t] < 0 ? -d[t] : d[t];
                if (v < amin) amin = v;
                if (v > amax) amax = v;
                asum += v;
            }
        }
        printf("\n  residual stats over %ld active dims of %u index vectors\n", nact, (unsigned)n);
        printf("    delta > 0 (sign bit set): %ld  (%.1f%%)\n", npos, 100.0*npos/nact);
        printf("    |delta|  min=%lld  mean=%lld  max=%lld\n", amin, asum/nact, amax);
    }

    const int Ks[4] = {2, 4, 8, 16};
    const int Bs[6] = {2, 3, 4, 6, 8, 0};
    printf("\n  rerank oracle at th=%d — ORDERING ONLY, accept/reject unchanged\n", th);
    printf("  baseline (coarse argmax): "); {
        int fa=0,wa=0,ms=0,ok=0;
        for (int i = 0; i < V_n; i++) {
            int c = (cbest[i] > th) ? r_apply_polarity(&R, R.label[cbi[i]], V_t[i]) : -1;
            const char *p = c < 0 ? "none" : R.names[c]; int gn = !strcmp(V_l[i], "none");
            if (!gn && !strcmp(p, V_l[i])) ok++;
            if (!strcmp(p, V_l[i])) continue;
            if (gn) fa++; else if (!strcmp(p, "none")) ms++; else wa++;
        }
        printf("fa=%d wa=%d missed=%d iot_ok=%d\n\n", fa, wa, ms, ok);
    }
    printf("  %-6s %-6s %5s %5s %8s %8s\n", "bits", "K", "fa", "wa", "missed", "iot_ok");
    for (int bi2 = 0; bi2 < 6; bi2++) {
        for (int ki = 0; ki < 4; ki++) {
            int B = Bs[bi2], K = Ks[ki];
            int fa=0,wa=0,ms=0,ok=0;
            for (int i = 0; i < V_n; i++) {
                int wj = cbi[i]; int64_t wbest = -((int64_t)1 << 60);
                if (cbest[i] > th) {
                    const int32_t *qq = qd + (size_t)i * RD;
                    for (int k = 0; k < K; k++) {
                        int j = top[i][k];
                        const int32_t *cc = RR_D + (size_t)j * RD;
                        int64_t sum = 0;
                        for (int t = 0; t < RD; t++) {
                            if (!qq[t] || !cc[t]) continue;
                            sum += (int64_t)rr_q(qq[t], qmx[i], B) * rr_q(cc[t], RR_MX[j], B);
                        }
                        int64_t sc = (2 * sum * RD) / (qab[i] + RR_AB[j] + RR_TSMOOTH);
                        if (sc > wbest) { wbest = sc; wj = j; }
                    }
                }
                int c = (cbest[i] > th) ? r_apply_polarity(&R, R.label[wj], V_t[i]) : -1;
                const char *p = c < 0 ? "none" : R.names[c]; int gn = !strcmp(V_l[i], "none");
                if (!gn && !strcmp(p, V_l[i])) ok++;
                if (!strcmp(p, V_l[i])) continue;
                if (gn) fa++; else if (!strcmp(p, "none")) ms++; else wa++;
            }
            printf("  %-6s %-6d %5d %5d %8d %8d\n",
                   B ? (char[4]){(char)('0'+B),0} : "full", K, fa, wa, ms, ok);
        }
    }
}


/* ---- abstention rule -----------------------------------------------------
 * The threshold is a single global bar on an absolute score. Oracle 1 showed
 * that costs 9 rank-1-correct commands to buy 32 rejections, and that the
 * competing negative is usually right there at rank 2. So test the relative
 * rule instead:
 *
 *     P = best score among POSITIVE (command) exemplars
 *     N = best score among NEGATIVE ("none") exemplars
 *     accept iff  P - N > margin
 *
 * "Is this more like a command than like the strongest evidence it is not one"
 * rather than "is this above 136". Subsumes the existing none-check: when the
 * nearest vector is a negative, P - N < 0 and any margin >= 0 rejects.
 *
 * Also sweeps the conjunction with the absolute bar, because the two rules may
 * be catching different things. Integer, deterministic, no new storage — both
 * quantities already exist inside the same scan. */
static int ABSTAIN = 0;

static void abstain(void) {
    int *P = malloc((size_t)V_n * sizeof(int));
    int *N = malloc((size_t)V_n * sizeof(int));
    int *C = malloc((size_t)V_n * sizeof(int));
    for (int i = 0; i < V_n; i++) {
        tvec q; t_encode(&R, V_t[i], &q); int aa = t_active(&q);
        int bp = -(1 << 28), bn = -(1 << 28); uint32_t bpi = 0;
        for (uint32_t j = 0; j < R.n_index; j++) {
            int s = t_score(&q, &TI[j], aa);
            if (!strcmp(R.names[R.label[j]], "none")) { if (s > bn) bn = s; }
            else if (s > bp) { bp = s; bpi = j; }
        }
        P[i] = bp; N[i] = bn;
        C[i] = r_apply_polarity(&R, R.label[bpi], V_t[i]);
    }

    printf("\n  abstention rule: accept iff P - N > margin\n");
    printf("  P = best command exemplar, N = best negative exemplar\n");
    {   /* computed, not asserted from memory: the previous version of this line
           was a hardcoded string that was never evaluated against anything. */
        int bfa=0,bwa=0,bms=0,bok=0;
        for (int i = 0; i < V_n; i++) {
            const char *p = (P[i] > RSHIP_TH && P[i] - N[i] > 0) ? R.names[C[i]] : "none";
            int gn = !strcmp(V_l[i], "none");
            if (!gn && !strcmp(p, V_l[i])) bok++;
            if (!strcmp(p, V_l[i])) continue;
            if (gn) bfa++; else if (!strcmp(p, "none")) bms++; else bwa++;
        }
        control_or_die("abstain", bfa, bwa, bms, bok, RSHIP_TH);
    }
    printf("\n");

    printf("  %7s %5s %5s %8s %8s\n", "margin", "fa", "wa", "missed", "iot_ok");
    for (int m = -40; m <= 80; m += 5) {
        int fa=0,wa=0,ms=0,ok=0;
        for (int i = 0; i < V_n; i++) {
            const char *p = (P[i] - N[i] > m) ? R.names[C[i]] : "none";
            int gn = !strcmp(V_l[i], "none");
            if (!gn && !strcmp(p, V_l[i])) ok++;
            if (!strcmp(p, V_l[i])) continue;
            if (gn) fa++; else if (!strcmp(p, "none")) ms++; else wa++;
        }
        printf("  %7d %5d %5d %8d %8d\n", m, fa, wa, ms, ok);
    }

    printf("\n  conjunction: accept iff P > th AND P - N > margin\n");
    printf("  %7s", "th\\marg");
    const int Ms[6] = {0, 10, 20, 30, 40, 60};
    for (int k = 0; k < 6; k++) printf("  %14d", Ms[k]);
    printf("\n");
    for (int th = 100; th <= 140; th += 10) {
        printf("  %7d", th);
        for (int k = 0; k < 6; k++) {
            int fa=0,wa=0,ms=0,ok=0;
            for (int i = 0; i < V_n; i++) {
                const char *p = (P[i] > th && P[i] - N[i] > Ms[k]) ? R.names[C[i]] : "none";
                int gn = !strcmp(V_l[i], "none");
                if (!gn && !strcmp(p, V_l[i])) ok++;
                if (!strcmp(p, V_l[i])) continue;
                if (gn) fa++; else if (!strcmp(p, "none")) ms++; else wa++;
            }
            printf("  %3d/%2d/%2d/%3d", fa, wa, ms, ok);
        }
        printf("\n");
    }
    printf("\n  cells are fa/wa/missed/iot_ok\n");

    /* The decision-relevant summary: for each fa budget, the best recall any
       (threshold, margin) pair reaches. Compare against the threshold-only
       curve, which is the m=-1000 column of this same search. */
    printf("\n  FRONTIER — best iot_ok at each fa budget\n");
    printf("  %6s  %30s   %30s\n", "fa<=", "shipped rule (m=0)", "with margin (best m)");
    for (int bud = 0; bud <= 6; bud++) {
        int b1=-1,b1th=0, b2=-1,b2th=0,b2m=0, b1w=0,b1s=0, b2w=0,b2s=0;
        for (int th = 0; th <= 220; th += 2) {
            for (int mi = -1; mi <= 100; mi += 1) {
                /* The SHIPPED rule is not "threshold the best positive" - it is
                   argmax over everything, reject if a negative won, else reject
                   if below th. That is exactly P>th AND P-N>0, i.e. the m=0
                   case. Comparing against m=-inf would be a strawman: it drops
                   the none-check the router already has. */
                int m = (mi < 0) ? 0 : mi;
                int fa=0,wa=0,ms=0,ok=0;
                for (int i = 0; i < V_n; i++) {
                    const char *p = (P[i] > th && P[i] - N[i] > m) ? R.names[C[i]] : "none";
                    int gn = !strcmp(V_l[i], "none");
                    if (!gn && !strcmp(p, V_l[i])) ok++;
                    if (!strcmp(p, V_l[i])) continue;
                    if (gn) fa++; else if (!strcmp(p, "none")) ms++; else wa++;
                }
                if (fa > bud) continue;
                if (mi < 0) { if (ok > b1) { b1=ok; b1th=th; b1w=wa; b1s=ms; } }
                else        { if (ok > b2) { b2=ok; b2th=th; b2m=m; b2w=wa; b2s=ms; } }
            }
        }
        printf("  %6d  th=%3d ok=%3d wa=%2d ms=%3d   th=%3d m=%3d ok=%3d wa=%2d ms=%3d\n",
               bud, b1th, b1, b1w, b1s, b2th, b2m, b2, b2w, b2s);
    }
    free(P); free(N); free(C);
}


/* ---- zero-FA forensics ---------------------------------------------------
 * The margin rule moved the fa<=1 frontier by 32 commands and the fa=0 frontier
 * by nothing. So at fa=0 something other than "weak evidence" or "a negative is
 * nearby" is setting the boundary. Find it.
 *
 * A negative is a false actuation at threshold th (margin 0) iff P > th and
 * P > N. So the negatives with P > N, sorted by P descending, ARE the frontier:
 * th must exceed the topmost one for fa=0. Dump them with everything that could
 * explain why the router believes they are commands. */
static int FAPROBE = 0;

static void faprobe(void) {
    typedef struct { int P, N, pi, ni, qi, aa, tot; } B;
    B *b = malloc((size_t)V_n * sizeof(B)); int nb = 0;
    for (int i = 0; i < V_n; i++) {
        if (strcmp(V_l[i], "none")) continue;             /* negatives only */
        tvec q; t_encode(&R, V_t[i], &q); int aa = t_active(&q);
        int16_t acc[RD]; int32_t tot; r_counts(V_t[i], acc, &tot);
        int bp = -(1 << 28), bn = -(1 << 28); int bpi = 0, bni = 0;
        for (uint32_t j = 0; j < R.n_index; j++) {
            int s = t_score(&q, &TI[j], aa);
            if (!strcmp(R.names[R.label[j]], "none")) { if (s > bn) { bn = s; bni = (int)j; } }
            else if (s > bp) { bp = s; bpi = (int)j; }
        }
        if (bp <= bn) continue;                            /* negative wins: safe */
        b[nb++] = (B){bp, bn, bpi, bni, i, aa, tot};
    }
    for (int i = 0; i < nb; i++)                           /* sort by P desc */
        for (int j = i + 1; j < nb; j++)
            if (b[j].P > b[i].P) { B t = b[i]; b[i] = b[j]; b[j] = t; }

    printf("\n  %d of %d dev negatives have P > N (a command exemplar outranks\n", nb, V_n);
    printf("  every negative). Sorted by P: the top one sets the fa=0 frontier,\n");
    printf("  because th must exceed it. Shipped th=136; fa=0 needs th=188.\n\n");
    int shown = nb < 12 ? nb : 12;
    for (int k = 0; k < shown; k++) {
        B *x = &b[k];
        printf("  [%2d] P=%-4d N=%-4d gap=%-4d  active=%-3d ngrams=%-4d  -> %s\n",
               k + 1, x->P, x->N, x->P - x->N, x->aa, x->tot,
               R.names[r_apply_polarity(&R, R.label[x->pi], V_t[x->qi])]);
        printf("       query    \"%s\"\n", V_t[x->qi]);
        printf("       nearest+ \"%s\"\n", U_t[x->pi]);
        printf("       nearest- \"%s\"\n", U_t[x->ni]);
    }
    printf("\n  gap distribution over all %d: ", nb);
    { int g0=0,g10=0,g25=0,g50=0,gbig=0;
      for (int k = 0; k < nb; k++) { int g = b[k].P - b[k].N;
        if (g < 10) g0++; else if (g < 25) g10++; else if (g < 50) g25++;
        else if (g < 75) g50++; else gbig++; }
      printf("<10:%d  10-24:%d  25-49:%d  50-74:%d  >=75:%d\n", g0,g10,g25,g50,gbig); }
    free(b);
}


/* ---- contrastive rescoring on the disagreement mask ----------------------
 * The blockers share an entire carrier phrase with their nearest command and
 * differ in one object word: "can you please put ON MUSIC" against "can you
 * please put THE VACUUM on". The shared frame contributes most of the n-grams,
 * so the whole-sentence similarity is dominated by what the two candidates have
 * in common — precisely the part that cannot distinguish them.
 *
 * So cancel it. Given the coarse winner A and a challenger B, let
 *     D = { i : (mask,sign) of A differs from B at i }
 * and rescore the query against each candidate over D alone. The question stops
 * being "which sentence does the query resemble" and becomes "given these two
 * readings, what in the query actually chooses between them".
 *
 * Four worlds, and the diagnostic tells us which:
 *   1. B wins on coarse-over-D            -> carrier dilution, nothing more
 *   2. B loses coarse-D, wins fine-D      -> magnitude IS the missing tier,
 *                                            and this is where it may speak
 *   3. B loses both                       -> the features do not encode it
 *   4. exact n-grams separate but hashed  -> hash collision / dimensionality,
 *      vectors do not                        not a scoring problem at all
 * World 4 matters: it would mean no metric over these vectors can help. */
static int CONTRAST = 0;

static int cnt_ng(const char *a, const char *b) {   /* shared distinct 3/4-grams */
    char na[512], nb[512];
    int la = r_norm(a, na, sizeof na), lb = r_norm(b, nb, sizeof nb);
    int sh = 0;
    for (int g = 3; g <= 4; g++)
        for (int i = 0; i + g <= la; i++) {
            int dup = 0;
            for (int k = 0; k < i && !dup; k++) if (!strncmp(na+k, na+i, g)) dup = 1;
            if (dup) continue;
            for (int j = 0; j + g <= lb; j++) if (!strncmp(na+i, nb+j, g)) { sh++; break; }
        }
    return sh;
}

/* A whole-word channel. The character channel sees "please start the vacuum"
 * and "please start the podcast" as nearly the same sentence, which is correct
 * and useful elsewhere. What it cannot say loudly enough is that the OBJECT
 * differs. Token identity says it very loudly and costs nothing: no weights, no
 * learning, no free parameters, same normaliser the encoder already uses. */
static int wordsim(const char *a, const char *b) {
    char na[512], nb[512];
    int la = r_norm(a, na, sizeof na), lb = r_norm(b, nb, sizeof nb);
    char *wa[128], *wb[128]; int ca = 0, cb = 0;
    for (int i = 0; i < la && ca < 128; ) {
        while (i < la && na[i] == ' ') i++;
        if (i >= la) break;
        wa[ca++] = na + i;
        while (i < la && na[i] != ' ') i++;
        if (i < la) na[i++] = 0;
    }
    for (int i = 0; i < lb && cb < 128; ) {
        while (i < lb && nb[i] == ' ') i++;
        if (i >= lb) break;
        wb[cb++] = nb + i;
        while (i < lb && nb[i] != ' ') i++;
        if (i < lb) nb[i++] = 0;
    }
    int sh = 0;
    for (int i = 0; i < ca; i++) {
        int dup = 0;
        for (int k = 0; k < i && !dup; k++) if (!strcmp(wa[k], wa[i])) dup = 1;
        if (dup) continue;
        for (int j = 0; j < cb; j++) if (!strcmp(wa[i], wb[j])) { sh++; break; }
    }
    return (ca + cb) ? (2 * sh * 1000) / (ca + cb) : 0;
}

static void contrast(void) {
    int th = FIXTH >= 0 ? FIXTH : RSHIP_TH;
    int w1=0,w2=0,w3=0,w4=0,skip=0,tot=0,wW=0,wW3=0;
    printf("\n  contrastive rescoring on the disagreement mask, K=8, th=%d\n", th);
    printf("  A = coarse winner (wrong)   B = best candidate that would be right\n\n");

    for (int i = 0; i < V_n; i++) {
        tvec q; t_encode(&R, V_t[i], &q); int aa = t_active(&q);
        int ts[8], ti[8];
        for (int k = 0; k < 8; k++) { ts[k] = -(1<<28); ti[k] = 0; }
        for (uint32_t j = 0; j < R.n_index; j++) {
            int s = t_score(&q, &TI[j], aa);
            if (s <= ts[7]) continue;
            int k = 7; while (k > 0 && ts[k-1] < s) { ts[k]=ts[k-1]; ti[k]=ti[k-1]; k--; }
            ts[k] = s; ti[k] = (int)j;
        }
        int c = (ts[0] > th) ? r_apply_polarity(&R, R.label[ti[0]], V_t[i]) : -1;
        const char *p = c < 0 ? "none" : R.names[c];
        if (!strcmp(p, V_l[i])) continue;
        int gn = !strcmp(V_l[i], "none");
        if (!gn && !strcmp(p, "none") && ts[0] <= th) continue;  /* threshold reject, not ordering */
        tot++;

        int B = -1;
        for (int k = 1; k < 8 && B < 0; k++) {
            const char *jn = gn ? R.names[R.label[ti[k]]]
                                : R.names[r_apply_polarity(&R, R.label[ti[k]], V_t[i])];
            if (!strcmp(jn, V_l[i])) B = ti[k];
        }
        if (B < 0) { skip++; continue; }
        int A = ti[0];

        /* D = dims where A and B differ; rescore query over D only */
        int dotA=0, dotB=0, aaD=0, abA=0, abB=0, nD=0;
        for (int d = 0; d < RD; d++) {
            int am = (TI[A].m[d>>5] >> (d&31)) & 1, as = (TI[A].s[d>>5] >> (d&31)) & 1;
            int bm = (TI[B].m[d>>5] >> (d&31)) & 1, bs = (TI[B].s[d>>5] >> (d&31)) & 1;
            if (am == bm && (!am || as == bs)) continue;      /* agree: cancel */
            nD++;
            int qm = (q.m[d>>5] >> (d&31)) & 1, qs = (q.s[d>>5] >> (d&31)) & 1;
            if (qm) aaD++;
            if (am) abA++;
            if (bm) abB++;
            if (qm && am) dotA += (qs == as) ? 1 : -2;
            if (qm && bm) dotB += (qs == bs) ? 1 : -2;
        }
        int sA = nD ? (2*dotA*RD)/(aaD+abA+8) : 0;
        int sB = nD ? (2*dotB*RD)/(aaD+abB+8) : 0;
        int ngA = cnt_ng(V_t[i], U_t[A]), ngB = cnt_ng(V_t[i], U_t[B]);
        int wsA = wordsim(V_t[i], U_t[A]), wsB = wordsim(V_t[i], U_t[B]);
        if (wsB > wsA) wW++;
        if (sB <= sA && dotB <= dotA && ngB <= ngA && wsB > wsA) wW3++;

        /* Two normalisations, because "rescore on D" does not specify one and
           that choice is exactly where the previous oracle went wrong. Dice-on-D
           divides by each candidate's own active count WITHIN D, which is
           asymmetric when candidates differ in length; raw dot does not
           normalise at all. Report both rather than quietly pick one. */
        if (sB > sA) w1++;
        if (dotB > dotA) w2++;
        if (sB <= sA && dotB <= dotA) { if (ngB > ngA) w4++; else w3++; }

        printf("  gap=%-4d |D|=%-4d  diceD A=%-5d B=%-5d  dotD A=%-4d B=%-4d  ng A=%-3d B=%-3d %s%s\n",
               ts[0]-ts[1], nD, sA, sB, dotA, dotB, ngA, ngB,
               sB > sA ? "DICE-FIX " : "", dotB > dotA ? "DOT-FIX" : "");
        printf("      word-channel A=%-5d B=%-5d %s\n", wsA, wsB, wsB > wsA ? "WORD-FIX" : "");
        printf("      q \"%s\"\n      A \"%s\"\n      B \"%s\"\n", V_t[i], U_t[A], U_t[B]);
    }
    printf("\n  of %d ordering errors with B in top-8 (%d had none):\n", tot-skip, skip);
    printf("    Dice-on-D puts the right answer first   : %d\n", w1);
    printf("    raw dot-on-D puts it first              : %d\n", w2);
    printf("    neither; exact n-grams DO separate      : %d  (hash/dimensionality)\n", w4);
    printf("    neither; exact n-grams do not either    : %d  (features lack it)\n", w3);
    printf("\n    WHOLE-WORD channel puts it first        : %d  of %d\n", wW, tot - skip);
    printf("    ...of the cases nothing else fixed      : %d\n", wW3);
}


/* ---- coverage audit -------------------------------------------------------
 * World 3 said the features do not separate these, and the whole-word tie
 * showed why: the query's discriminative token is absent from BOTH candidates.
 * So ask the question directly, against the FULL index rather than the top-K:
 *
 *   for each token of a failing query, does it occur in ANY exemplar of the
 *   true class? anywhere in the index at all?
 *
 * Three outcomes, and they are different problems:
 *   df_true > 0   the vocabulary IS in the class - a selection/ranking failure,
 *                 and the one case where a class-conditioned channel (SSTT's
 *                 information gain, not IDF) could vote correctly without any
 *                 candidate containing the token
 *   df_true = 0, df_all > 0   the word exists but never for this class
 *   df_all  = 0   open vocabulary: no exemplar set contains it, and no
 *                 statistic over the corpus can bridge it */
static int COVERAGE = 0;

static int tok_split(const char *s, char *buf, int cap, char **w, int maxw) {
    int l = r_norm(s, buf, cap), c = 0;
    for (int i = 0; i < l && c < maxw; ) {
        while (i < l && buf[i] == ' ') i++;
        if (i >= l) break;
        w[c++] = buf + i;
        while (i < l && buf[i] != ' ') i++;
        if (i < l) buf[i++] = 0;
    }
    return c;
}

static void coverage(void) {
    int th = FIXTH >= 0 ? FIXTH : RSHIP_TH;
    int nsel = 0, ncorp = 0, nopen = 0, ncase = 0;
    printf("\n  coverage audit against the FULL %u-vector index, th=%d\n", (unsigned)R.n_index, th);
    printf("  for each failing command, where does its vocabulary live?\n\n");
    for (int i = 0; i < V_n; i++) {
        if (!strcmp(V_l[i], "none")) continue;              /* commands only */
        tvec q; t_encode(&R, V_t[i], &q); int aa = t_active(&q);
        int best = -(1<<28); uint32_t bi = 0;
        for (uint32_t j = 0; j < R.n_index; j++) {
            int s = t_score(&q, &TI[j], aa);
            if (s > best) { best = s; bi = j; }
        }
        int c = (best > th) ? r_apply_polarity(&R, R.label[bi], V_t[i]) : -1;
        const char *p = c < 0 ? "none" : R.names[c];
        if (!strcmp(p, V_l[i])) continue;
        ncase++;

        char buf[512]; char *w[64];
        int nw = tok_split(V_t[i], buf, sizeof buf, w, 64);
        int any_all = 0, rare_in_class = 0;
        char rep[256]; rep[0] = 0;
        for (int t = 0; t < nw; t++) {
            if (strlen(w[t]) < 4) continue;                 /* skip carrier scaffolding */
            int dt = 0, da = 0;
            char eb[512]; char *ew[64];
            for (uint32_t j = 0; j < R.n_index; j++) {
                int ne = tok_split(U_t[j], eb, sizeof eb, ew, 64), hit = 0;
                for (int k = 0; k < ne && !hit; k++) if (!strcmp(ew[k], w[t])) hit = 1;
                if (!hit) continue;
                da++;
                if (!strcmp(R.names[R.label[j]], V_l[i])) dt++;
            }
            if (da) any_all = 1;
            if (dt && da <= 40) rare_in_class = 1;
            char one[64];
            snprintf(one, sizeof one, "%s(%d/%d) ", w[t], dt, da);
            if (strlen(rep) + strlen(one) < sizeof rep - 1) strcat(rep, one);
        }
        const char *verdict;
        if (rare_in_class)      { verdict = "SELECTION/RANKING - discriminative token IS in class"; nsel++; }
        else if (any_all)       { verdict = "CORPUS - word exists, never for this class";           ncorp++; }
        else                    { verdict = "OPEN VOCAB - absent from the corpus entirely";         nopen++; }
        printf("  %-52s said=%-20s truth=%s\n", V_t[i], p, V_l[i]);
        printf("      tokens(in-class/anywhere): %s\n      -> %s\n", rep, verdict);
    }
    printf("\n  of %d failing commands:\n", ncase);
    printf("    discriminative token present in the true class : %d\n", nsel);
    printf("    word exists in corpus but never for that class : %d\n", ncorp);
    printf("    absent from the corpus entirely                : %d\n", nopen);
}


/* ---- Program B: uncertainty-gated lexical corroboration -------------------
 * Three previous attempts to combine the word channel with the router failed
 * (signature prior inert; word prior 3 tie/2 lose/3 win; the gate/selector
 * failed HELD-OUT and was cut in eval #4). The signal is not in doubt - the
 * prior scores 89.6% against the router's 85.9% on dev IoT. What failed was
 * authority: a useful classifier is not automatically a useful arbiter.
 *
 * So give it less. Two asymmetries:
 *
 *   1. THE ROUTER DECIDES WHEN THE SPECIALIST SPEAKS. The gate is the router's
 *      own uncertainty - how far its best score sits below its own threshold -
 *      not a confidence the auxiliary channel reports about itself. That is the
 *      structural difference from --gatesel, which gated on the prior's margin.
 *
 *   2. THE SPECIALIST MAY ONLY CORROBORATE. It can rescue a command the router
 *      already identified and was too timid to actuate. It cannot propose a
 *      different class, and it can never turn a "none" into an actuation - the
 *      failure that took fa from 1 to 824 when the prior was given that power.
 *
 * Targets the largest error class directly: nine of fourteen dev misses are
 * rank-1 correct and rejected by the threshold alone. */
static int CORROB = 0;

static void corrob(void) {
    int th = FIXTH >= 0 ? FIXTH : RSHIP_TH;
    int *P = malloc((size_t)V_n * sizeof(int));
    int *Nn = malloc((size_t)V_n * sizeof(int));
    int *C = malloc((size_t)V_n * sizeof(int));
    int *PV = malloc((size_t)V_n * sizeof(int));
    int *PM = malloc((size_t)V_n * sizeof(int));
    for (int i = 0; i < V_n; i++) {
        tvec q; t_encode(&R, V_t[i], &q); int aa = t_active(&q);
        int bp = -(1 << 28), bn = -(1 << 28); uint32_t bpi = 0;
        for (uint32_t j = 0; j < R.n_index; j++) {
            int s = t_score(&q, &TI[j], aa);
            if (!strcmp(R.names[R.label[j]], "none")) { if (s > bn) bn = s; }
            else if (s > bp) { bp = s; bpi = j; }
        }
        P[i] = bp; Nn[i] = bn;
        C[i] = r_apply_polarity(&R, R.label[bpi], V_t[i]);
        int mg = 0; PV[i] = pr_vote(&PR, V_t[i], &mg); PM[i] = mg;
    }

    printf("\n  Program B: uncertainty-gated lexical corroboration, th=%d\n", th);
    printf("  rescue a REJECTED command only when the word prior independently\n");
    printf("  names the SAME class the router already chose. No class override,\n");
    printf("  no rescue of a \"none\" verdict.\n\n");
    {   int bfa=0,bwa=0,bms=0,bok=0;
        for (int i = 0; i < V_n; i++) {
            const char *p = (P[i] > th && P[i] > Nn[i]) ? R.names[C[i]] : "none";
            int gn = !strcmp(V_l[i], "none");
            if (!gn && !strcmp(p, V_l[i])) bok++;
            if (!strcmp(p, V_l[i])) continue;
            if (gn) bfa++; else if (!strcmp(p, "none")) bms++; else bwa++;
        }
        control_or_die("corrob", bfa, bwa, bms, bok, th);
    }
    printf("  %-8s %-8s %5s %5s %8s %8s   %s\n",
           "band", "prmargin", "fa", "wa", "missed", "iot_ok", "note");


    const int Ds[7] = {0, 8, 16, 24, 32, 48, 1000};
    const int Ms[3] = {0, 64, 128};
    for (int mi = 0; mi < 3; mi++) {
        for (int di = 0; di < 7; di++) {
            int D = Ds[di], M = Ms[mi];
            int fa=0,wa=0,ms=0,ok=0;
            for (int i = 0; i < V_n; i++) {
                /* The SHIPPED rule: argmax over everything, reject if a
                   negative won, else reject if below th. "A negative won" is
                   P <= N. Thresholding the best positive alone would drop the
                   none-check and is the strawman METHOD 19 exists for. */
                int acc = (P[i] > th) && (P[i] > Nn[i]);
                /* Rescue ONLY the timid case: the router had a command in hand
                   (it outranked every negative) and merely sat below its own
                   bar. A "none" verdict is a different rejection and is never
                   overridden - that is the arbitration this design refuses. */
                if (!acc && P[i] > Nn[i] && P[i] <= th && P[i] > th - D
                    && PV[i] == C[i] && PM[i] >= M)
                    acc = 1;
                const char *p = acc ? R.names[C[i]] : "none";
                int gn = !strcmp(V_l[i], "none");
                if (!gn && !strcmp(p, V_l[i])) ok++;
                if (!strcmp(p, V_l[i])) continue;
                if (gn) fa++; else if (!strcmp(p, "none")) ms++; else wa++;
            }
            printf("  %-8d %-8d %5d %5d %8d %8d   %s\n", D, M, fa, wa, ms, ok,
                   (di == 0 && mi == 0) ? "<- shipped (no rescue)" : "");
        }
    }
    /* The raw table above compares against ONE shipped point. That is not the
       question. Corroboration buys commands and costs false actuations, and so
       does simply lowering the threshold - so the honest test is at MATCHED fa
       against the best the existing mechanism can reach. METHOD 19. */
    printf("\n  FRONTIER - best iot_ok at each fa budget\n");
    printf("  %6s   %22s   %28s\n", "fa<=", "threshold alone", "threshold + corroboration");
    for (int bud = 6; bud <= 20; bud += 2) {
        int b1 = -1, b1t = 0, b2 = -1, b2t = 0, b2d = 0, b2m = 0;
        for (int t = 90; t <= 200; t += 2) {
            for (int di = -1; di < 7; di++) {
                for (int mi = 0; mi < 3; mi++) {
                    if (di < 0 && mi > 0) continue;
                    int D = di < 0 ? 0 : Ds[di], M = di < 0 ? 0 : Ms[mi];
                    int fa=0,ok=0;
                    for (int i = 0; i < V_n; i++) {
                        int acc = (P[i] > t) && (P[i] > Nn[i]);
                        if (!acc && di >= 0 && P[i] > Nn[i] && P[i] <= t && P[i] > t - D
                            && PV[i] == C[i] && PM[i] >= M) acc = 1;
                        const char *p = acc ? R.names[C[i]] : "none";
                        int gn = !strcmp(V_l[i], "none");
                        if (!gn && !strcmp(p, V_l[i])) ok++;
                        if (strcmp(p, V_l[i]) && gn) fa++;
                    }
                    if (fa > bud) continue;
                    if (di < 0) { if (ok > b1) { b1 = ok; b1t = t; } }
                    else        { if (ok > b2) { b2 = ok; b2t = t; b2d = D; b2m = M; } }
                }
            }
        }
        printf("  %6d   th=%3d ok=%3d          th=%3d D=%4d M=%3d ok=%3d   %s\n",
               bud, b1t, b1, b2t, b2d, b2m, b2, b2 > b1 ? "corrob wins" : (b2 == b1 ? "tie" : ""));
    }

    free(P); free(Nn); free(C); free(PV); free(PM);
}

/* ---- METHOD 19, enforced ---------------------------------------------------
 * "Does the baseline reproduce the product?" cost an order-of-magnitude
 * inflated claim once (+38 commands instead of +3) and was violated a second
 * time three experiments later, in the same way: thresholding the best positive
 * while ignoring whether a negative outranked it, which silently deletes the
 * none-check the router already has.
 *
 * Twice is not a discipline problem. So the rule is no longer a rule:
 * a comparison CANNOT EXIST unless its control is valid. Any experiment that
 * reports a treatment must first hand its own computed baseline to this
 * function, and if the baseline does not reproduce the shipped numbers the
 * process exits before a single treatment figure is printed. */
static void control_or_die(const char *what, int fa, int wa, int ms, int ok, int th) {
    if (FIXTH != RSHIP_TH || PRUNE.neg_top != RSHIP_NEGTOP || PRUNE.neg_bound
        || PRUNE.neg_halo || PRUNE.cnn || PRUNE.dup || PRUNE.neg_k > 1) {
        printf("  [control] not the shipped configuration — assertion skipped\n");
        return;
    }
    if (fa == 6 && wa == 13 && ms == 14 && ok == 165 && th == RSHIP_TH) {
        printf("  [control] %s baseline reproduces the product: "
               "fa=6 wa=13 missed=14 iot_ok=165 th=%d\n", what, th);
        return;
    }
    fprintf(stderr,
        "\n  *** CONTROL FAILED (%s) — METHOD 19 ***\n"
        "      this experiment's baseline gave  fa=%d wa=%d missed=%d ok=%d th=%d\n"
        "      the shipped product gives        fa=6 wa=13 missed=14 ok=165 th=%d\n"
        "      The control does not reproduce the product, so any treatment\n"
        "      number computed against it is meaningless. Aborting before one\n"
        "      is printed.\n\n", what, fa, wa, ms, ok, th, RSHIP_TH);
    exit(2);
}


/* ---- per-item disposition dump -------------------------------------------
 * A headline of "12 -> 11" is a difference of marginals and hides which
 * utterances changed. Two selectors could repair one and break none, or churn
 * six and break five, and those are different engineering results. The
 * observations are PAIRED - same utterances, both runs - so the transition
 * table is the right object, and an exact McNemar on the discordant cells is
 * the right test if one is wanted.
 *
 * Dump one line per evaluated item so two runs can be diffed. Deliberately not
 * a two-index diagnostic inside one process: the split is chosen by --test, and
 * a tool that silently evaluated the held-out set twice would spend budget
 * without it appearing in the audit log as two touches. */
static int DUMPDISP = 0;

static void dumpdisp(void) {
    int th = FIXTH >= 0 ? FIXTH : RSHIP_TH;
    char **T = USE_TEST ? T_t : V_t;
    char (*L)[RNAMELEN] = USE_TEST ? T_l : V_l;
    int   N = USE_TEST ? T_n : V_n;
    /* This dump feeds a treatment/control comparison, so it is subject to
       METHOD 19 like any other. Under the shipped selector it must reproduce
       the product; under a treatment selector control_or_die skips with an
       explicit message, which is the correct behaviour for the treatment arm.
       Asserted BEFORE any line is emitted - checking a control after you
       already believe the treatment is not checking it. */
    if (!USE_TEST) {
        int bfa=0,bwa=0,bms=0,bok=0;
        for (int i = 0; i < N; i++) {
            hit h = score_ter(T[i]);
            int c = (h.score > th) ? h.cls : -1;
            const char *p = c < 0 ? "none" : R.names[c];
            int gn = !strcmp(L[i], "none");
            if (!gn && !strcmp(p, L[i])) bok++;
            if (!strcmp(p, L[i])) continue;
            if (gn) bfa++; else if (!strcmp(p, "none")) bms++; else bwa++;
        }
        control_or_die("dumpdisp", bfa, bwa, bms, bok, th);
    }
    for (int i = 0; i < N; i++) {
        hit h = score_ter(T[i]);
        int c = (h.score > th) ? h.cls : -1;
        const char *p = c < 0 ? "none" : R.names[c];
        printf("DISP\t%d\t%s\t%s\t%s\n", i, L[i], p, T[i]);
    }
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
    printf("      %-21s overall: fixed %-3d broke %-3d  p=%.4f (two-sided exact) %s\n", "",
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
"  --prune-negtop=N       keep the N negatives with the highest NN-coverage\n"
"  --prune-negbound=N     keep the N negatives that sit on POSITIVE boundaries\n"
"  --prune-neghalo=N      per-class halo allocation. MEASURED EQUIVALENT to negbound\n"
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
"  --packbench            packed sign plane: cycles vs bytes, bit-exact check\n"
"  --rankoracle           for each dev error, rank of the answer that would be right\n"
"  --rerankoracle         can the discarded per-dim magnitude reorder the top-K?\n"
"  --condcentre           per-dim centre conditioned on firing, not diluted by zeros\n"
"  --abstain              accept on P-N margin instead of an absolute threshold\n"
"  --faprobe              the negatives that pin the zero-FA frontier, forensically\n"
"  --contrast             rescore top-2 on the dims where they disagree\n"
"  --coverage             for failing commands, where does their vocabulary live?\n"
"  --corrob               word prior may CORROBORATE a rejected command, never override\n"
"  --dumpdisp             one line per item: truth, prediction. Diff two runs\n"
"  --selsig               paired significance of the selector on dev\n"
"  --seldump --dirdump    per-item signal dumps (TSV)\n"
"  --prune-diag           per-class guard coverage of the surviving negatives\n"
"  --asym                 asymmetric threshold (126 up / 136 down) ... rejected\n"
"  --footprint            LMM footprint cycle: where the 656 KB actually goes\n"
"  --lmm-raw[3]           LMM cycle artifacts, raw capture\n"
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
/* --footprint: what the solution actually costs in bytes, and what each
 * encoding would cost. Speed is a side-effect here; the metric is size. */
static void footprint(void) {
    long act_tot = 0, pairs = 0;
    int mn = 1 << 30, mx = 0;
    for (int i = 0; i < U_n; i++) {
        int a = t_active(&TI[i]);
        act_tot += a; if (a < mn) mn = a; if (a > mx) mx = a;
    }
    double avg = (double)act_tot / U_n;
    double p = avg / RD;
    double H = -(p * log2(p) + (1 - p) * log2(1 - p));      /* bits per dim, mask */
    double mask_bits = RD * H;                              /* entropy of the mask plane */
    double sign_bits = avg;                                 /* one bit per ACTIVE dim only */
    (void)pairs;

    printf("\n  === footprint, measured ===\n\n");
    printf("  index                    %6.1f KB   %d vectors x %zu B\n",
           U_n * (double)sizeof(tvec) / 1024, U_n, sizeof(tvec));
    printf("  popcount table              0.25 KB   DRAM\n");
    printf("  firmware code            ~%4.0f KB   flash\n", 78.0);
    printf("  ------------------------------------------------\n");
    printf("  total on a 4 MB part     %6.1f KB   (%.1f%% of flash)\n\n",
           U_n * (double)sizeof(tvec) / 1024 + 78.25,
           100 * (U_n * (double)sizeof(tvec) + 80128) / (4 * 1048576.0));

    printf("  per vector, where the %zu bytes go:\n", sizeof(tvec));
    printf("    mask plane   %2d B   %d bits, %.1f set on average (%.1f%% density)\n",
           RD / 8, RD, avg, 100 * p);
    printf("    sign plane   %2d B   %d bits, but only %.1f carry meaning\n",
           RD / 8, RD, avg);
    printf("                        -> %.1f B of the sign plane is provable waste\n\n",
           (RD - avg) / 8);

    printf("  encodings, bytes per vector:\n");
    printf("    current bit-planes          %2zu.0 B   two %d-bit planes\n", sizeof(tvec), RD);
    printf("    signs packed to active dims %4.1f B   mask %d B + %.0f sign bits\n",
           RD / 8 + avg / 8, RD / 8, avg);
    printf("    mask entropy-coded + signs  %4.1f B   %.1f mask bits + %.0f sign bits\n",
           (mask_bits + sign_bits) / 8, mask_bits, sign_bits);
    printf("    sparse u8 indices + signs   %4.1f B   %.0f x (8-bit idx + 1 sign bit)\n",
           avg * 9 / 8, avg);
    printf("\n  index size under each, and %% of the 4 MB part:\n");
    double e[4] = { (double)sizeof(tvec), RD / 8 + avg / 8, (mask_bits + sign_bits) / 8, avg * 9 / 8 };
    const char *nm[4] = { "bit-planes (now)", "packed signs", "entropy-coded", "sparse indices" };
    for (int i = 0; i < 4; i++)
        printf("    %-26s %6.1f KB   %4.1f%%   %.2fx\n", nm[i], U_n * e[i] / 1024,
               100 * U_n * e[i] / (4 * 1048576.0), e[0] / e[i]);
}
/* RAW for the footprint LMM cycle. Three questions, measured:
 *   1. Is the SOURCE TEXT smaller than the vector it produces?
 *   2. Do index vectors resemble each other enough to delta-encode?
 *   3. What is the irreducible content, if the mask is derivable from text?  */
static void lmm_raw(void) {
    long chars = 0; int mx = 0;
    for (int i = 0; i < U_n; i++) { int l = strlen(U_t[i]); chars += l; if (l > mx) mx = l; }
    double avgc = (double)chars / U_n;
    printf("\n  === RAW 1: text vs vector ===\n");
    printf("    stored vector            %2zu B\n", sizeof(tvec));
    printf("    source text        %8.1f B average, %d max\n", avgc, mx);
    printf("    text is %.2fx %s than the vector it generates\n",
           avgc < sizeof(tvec) ? sizeof(tvec)/avgc : avgc/sizeof(tvec),
           avgc < sizeof(tvec) ? "SMALLER" : "larger");
    printf("    whole corpus as text     %6.1f KB  vs  %6.1f KB as vectors\n",
           chars / 1024.0, U_n * (double)sizeof(tvec) / 1024);

    /* 2. how alike are neighbours? delta-encode each vector against its nearest
          predecessor and count the set bits that survive. */
    printf("\n  === RAW 2: are vectors compressible against each other? ===\n");
    long raw_bits = 0, delta_bits = 0; int n = U_n < 3000 ? U_n : 3000;
    for (int i = 1; i < n; i++) {
        int best = -(1<<28), bj = 0;
        int aa = t_active(&TI[i]);
        for (int j = 0; j < i && j < 400; j++) {          /* nearest among recent */
            int s = t_score(&TI[i], &TI[j], aa);
            if (s > best) { best = s; bj = j; }
        }
        for (int w = 0; w < RWORDS; w++) {
            raw_bits   += __builtin_popcount(TI[i].m[w]) + __builtin_popcount(TI[i].s[w]);
            delta_bits += __builtin_popcount(TI[i].m[w] ^ TI[bj].m[w])
                        + __builtin_popcount(TI[i].s[w] ^ TI[bj].s[w]);
        }
    }
    printf("    set bits, stored as-is        %8ld over %d vectors\n", raw_bits, n-1);
    printf("    set bits after XOR vs nearest %8ld  (%.1f%% of raw)\n",
           delta_bits, 100.0 * delta_bits / raw_bits);
    printf("    -> delta encoding %s\n",
           delta_bits < raw_bits ? "REDUCES the population count" : "does NOT help");
}
/* RAW 3: the index costs 656 KB. The 74 KB gate already classifies IoT better
 * than the router (89.6% vs 85.9%) but cannot REJECT. So what does rejection
 * actually need? If some cheap signal separates commands from non-commands,
 * the index's only irreplaceable job disappears and the footprint collapses.
 *
 * Candidates, all free once the gate exists: prior margin, informative-word
 * count, utterance length. */
static void lmm_raw3(void) {
    printf("\n  === RAW 3: what does REJECTION need? ===\n");
    printf("    %-22s %-18s %-18s %s\n", "signal", "commands (mean)", "non-commands", "separation");
    long n_iot=0, n_neg=0;
    double mg_iot=0, mg_neg=0, wl_iot=0, wl_neg=0, sc_iot=0, sc_neg=0;
    for (int i = 0; i < V_n; i++) {
        int gn = !strcmp(V_l[i], "none");
        int mg; pr_vote(&PR, V_t[i], &mg);
        int w = 1; for (const char *p = V_t[i]; *p; p++) if (*p==' ') w++;
        hit h = score_ter(V_t[i]);
        if (gn) { n_neg++; mg_neg += mg; wl_neg += w; sc_neg += h.score; }
        else    { n_iot++; mg_iot += mg; wl_iot += w; sc_iot += h.score; }
    }
    printf("    %-22s %-18.1f %-18.1f %.2fx\n", "prior margin", mg_iot/n_iot, mg_neg/n_neg, (mg_iot/n_iot)/(mg_neg/n_neg));
    printf("    %-22s %-18.1f %-18.1f %.2fx\n", "words", wl_iot/n_iot, wl_neg/n_neg, (wl_iot/n_iot)/(wl_neg/n_neg));
    printf("    %-22s %-18.1f %-18.1f %.2fx\n", "router score (656 KB)", sc_iot/n_iot, sc_neg/n_neg, (sc_iot/n_iot)/(sc_neg/n_neg));

    /* how well can prior margin ALONE reject, swept as a threshold? */
    printf("\n    prior margin as a rejector, on its own:\n");
    printf("    %-8s %-10s %-10s %s\n", "margin>=", "fa", "missed", "iot recall");
    for (int t = 0; t <= 40; t += 8) {
        int fa=0, ms=0, ok=0, iot=0;
        for (int i = 0; i < V_n; i++) {
            int gn = !strcmp(V_l[i], "none");
            int mg; int pv = pr_vote(&PR, V_t[i], &mg);
            int act = (pv >= 0 && mg >= t && strcmp(R.names[pv],"none"));
            if (!gn) { iot++; if (act && !strcmp(R.names[pv], V_l[i])) ok++; else if (!act) ms++; }
            else if (act) fa++;
        }
        printf("    %-8d %-10d %-10d %.1f%%\n", t, fa, ms, 100.0*ok/iot);
    }
    printf("\n    router at th=136 for comparison:      fa=1   missed=14   85.9%%\n");
}
/* REFLECT: the index is 89% negatives, and they exist only so the router can
 * say "no". Positives get exact nearest-neighbour; negatives get 9345 stored
 * examples of what a non-command looks like.
 *
 * ASYMMETRIC IDEA: keep exact NN for the 1155 IoT vectors (74 KB) and replace
 * the 9345 negatives with a statistical model of none-ness — the word prior's
 * "none" evidence, which costs nothing extra because the gate already exists.
 *
 * Reject if the prior's none-score beats its best IoT score by NVETO. */
static void asym(void) {
    int nonec = -1;
    for (uint32_t c = 0; c < R.n_class; c++) if (!strcmp(R.names[c], "none")) nonec = (int)c;
    printf("\n  === asymmetric: exact positives + statistical negatives ===\n");
    printf("    IoT vectors in index : %d\n", (int)(U_n - 0));
    int iotn = 0; for (int i = 0; i < U_n; i++) if ((int)R.label[i] != nonec) iotn++;
    printf("    positives %d (%.0f KB)   negatives %d (%.0f KB)\n",
           iotn, iotn*64/1024.0, U_n-iotn, (U_n-iotn)*64/1024.0);
    printf("\n    threshold sweep on POSITIVES ONLY (72 KB), veto off then on:\n");
    printf("    %-8s %-10s %-8s %-8s %-8s %s\n", "th", "none-veto", "fa", "wa", "missed", "recall");
    for (int vi = 0; vi < 2; vi++)
    for (int th = 150; th <= 210; th += 15) {
        int nv = vi ? 0 : 9999;
        int fa=0, wa=0, ms=0, ok=0, iot=0;
        for (int i = 0; i < V_n; i++) {
            int gn = !strcmp(V_l[i], "none");
            /* stage 1: nearest neighbour over POSITIVES ONLY */
            tvec q; t_encode(&R, V_t[i], &q); int aa = t_active(&q);
            int best = -(1<<28); uint32_t bi = 0;
            for (int k = 0; k < U_n; k++) {
                if ((int)R.label[k] == nonec) continue;
                int s = t_score(&q, &TI[k], aa);
                if (s > best) { best = s; bi = k; }
            }
            /* stage 2: statistical none-veto from the word channel */
            int mg; int pv = pr_vote(&PR, V_t[i], &mg);
            int none_wins = (pv >= 0 && pv == nonec) ? mg : -mg;
            int act = (best > th) && (none_wins < nv);
            const char *pred = act ? R.names[r_apply_polarity(&R, R.label[bi], V_t[i])] : "none";
            if (!gn) { iot++; if (!strcmp(pred, V_l[i])) ok++; }
            if (!strcmp(pred, V_l[i])) continue;
            if (gn) fa++; else if (!strcmp(pred,"none")) ms++; else wa++;
        }
        printf("    %-8d %-10s %-8d %-8d %-8d %.1f%%\n", th, vi?"on":"off", fa, wa, ms, 100.0*ok/iot);
    }
    printf("\n    full 656 KB index for comparison:  fa=1  wa=13  missed=14  85.9%%\n");
    printf("    iot-only + threshold (measured):   fa=11 ...  29 missed at wrong<=16\n");
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
/* --packbench: does a packed sign plane pay for its cycles?
 *
 * The sign plane stores RD bits of which only ~22.6% carry meaning (the rest
 * sit under a zero mask bit and are never read). Packing signs down to the
 * active dims alone gives  RD/8 B mask + ceil(active/8) B signs  — 39.2 B on
 * this index instead of 64 B, 1.63x smaller.
 *
 * The cost: to read the sign of dim k you need its RANK inside the mask (how
 * many set mask-bits precede it). That is a per-set-bit loop, not RWORDS
 * branchless word ops. Two ways to pay it, both measured here:
 *   EXPAND — scatter the packed signs back into RWORDS words (loop over every
 *            active dim, ~58), then run the ordinary branchless dot.
 *   RANK   — loop only over the INTERSECTION bits and rank-index each one.
 *            Fewer iterations, more work per iteration.
 * Portable C only: no PDEP/BMI2, because Xtensa LX6 has no scatter instruction
 * and a host-only intrinsic would measure a machine we do not ship on.
 *
 * Exactness is checked over the FULL cross product (every dev query x every
 * index vector), not sampled. */
#ifndef TSMOOTH
#define TSMOOTH 8      /* must track ternary.c; the exactness check enforces it */
#endif
#define PB4(n)  n, n+1, n+1, n+2
#define PB6(n)  PB4(n), PB4(n+1), PB4(n+1), PB4(n+2)
#define PB8(n)  PB6(n), PB6(n+1), PB6(n+1), PB6(n+2)
/* same 8-bit table ternary.c uses, so both sides pay the same popcount price */
static const uint8_t PBPC8[256] = { PB8(0), PB8(1), PB8(1), PB8(2) };
static inline int pbpc(uint32_t x) {
    return PBPC8[x & 0xff] + PBPC8[(x >> 8) & 0xff]
         + PBPC8[(x >> 16) & 0xff] + PBPC8[x >> 24];
}
/* sp[] is sized for the worst case (every dim active). The STORED size is
   RD/8 + ceil(active/8); that is what the footprint arithmetic below uses. */
typedef struct { uint32_t m[RWORDS]; uint8_t sp[RD / 8]; } pvec;

static void pb_pack(const tvec *v, pvec *p) {
    memcpy(p->m, v->m, sizeof p->m);
    memset(p->sp, 0, sizeof p->sp);
    int r = 0;
    for (int i = 0; i < RWORDS; i++) {
        uint32_t m = v->m[i];
        while (m) {
            uint32_t low = m & (~m + 1u);
            if (v->s[i] & low) p->sp[r >> 3] |= (uint8_t)(1u << (r & 7));
            r++; m ^= low;
        }
    }
}
/* EXPAND: rebuild the sign plane, then the shipped branchless loop verbatim */
static int pb_dot_expand(const tvec *a, const pvec *b) {
    uint32_t s[RWORDS];
    int r = 0;
    for (int i = 0; i < RWORDS; i++) {
        uint32_t m = b->m[i], o = 0;
        while (m) {
            uint32_t low = m & (~m + 1u);
            o |= low & (uint32_t)(-(int32_t)((b->sp[r >> 3] >> (r & 7)) & 1));
            r++; m ^= low;
        }
        s[i] = o;
    }
    int agree = 0, dis = 0;
    for (int i = 0; i < RWORDS; i++) {
        uint32_t both = a->m[i] & b->m[i];
        uint32_t diff = a->s[i] ^ s[i];
        agree += pbpc(both);
        dis   += pbpc(both & diff);
    }
    return agree - 2 * dis;
}
/* RANK: iterate the intersection only; rank = words-so-far + bits-below */
static int pb_dot_rank(const tvec *a, const pvec *b) {
    int agree = 0, dis = 0, base = 0;
    for (int i = 0; i < RWORDS; i++) {
        uint32_t mb = b->m[i];
        uint32_t both = a->m[i] & mb;
        uint32_t sa = a->s[i], t = both;
        agree += pbpc(both);
        while (t) {
            uint32_t low = t & (~t + 1u);
            int r = base + pbpc(mb & (low - 1u));
            int sb = (b->sp[r >> 3] >> (r & 7)) & 1;
            dis += ((sa & low) ? 1 : 0) ^ sb;
            t ^= low;
        }
        base += pbpc(mb);
    }
    return agree - 2 * dis;
}
/* RANK+: the strongest packed form we could construct. The plain RANK loop
   re-popcounts every mask word to advance the running rank — the same 8
   redundant popcounts BLOB_FORMAT.md priced at 569 of 2335 cycles. Precompute
   the per-word prefix rank at BUILD time instead (RWORDS bytes/vector), so the
   inner loop does one popcount for the bits-below and nothing else. Stored
   bytes rise to RD/8 + RWORDS + ceil(active/8), still smaller than 64. */
typedef struct { uint32_t m[RWORDS]; uint8_t rk[RWORDS]; uint8_t sp[RD / 8]; } pvec2;
static void pb_pack2(const tvec *v, pvec2 *p) {
    memcpy(p->m, v->m, sizeof p->m);
    memset(p->sp, 0, sizeof p->sp);
    int r = 0;
    for (int i = 0; i < RWORDS; i++) {
        p->rk[i] = (uint8_t)r;
        uint32_t m = v->m[i];
        while (m) {
            uint32_t low = m & (~m + 1u);
            if (v->s[i] & low) p->sp[r >> 3] |= (uint8_t)(1u << (r & 7));
            r++; m ^= low;
        }
    }
}
static int pb_dot_rank2(const tvec *a, const pvec2 *b) {
    int agree = 0, dis = 0;
    for (int i = 0; i < RWORDS; i++) {
        uint32_t mb = b->m[i];
        uint32_t both = a->m[i] & mb;
        uint32_t sa = a->s[i], t = both;
        int base = b->rk[i];
        agree += pbpc(both);
        while (t) {
            uint32_t low = t & (~t + 1u);
            int r = base + pbpc(mb & (low - 1u));
            dis += ((sa & low) ? 1 : 0) ^ ((b->sp[r >> 3] >> (r & 7)) & 1);
            t ^= low;
        }
    }
    return agree - 2 * dis;
}
static inline int pb_score(int d, int aa, int ab) {
    return (2 * d * 256) / (aa + ab + TSMOOTH);
}
static double pb_ns(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}
static void packbench(void) {
    int nq = V_n; if (nq > 512) nq = 512;      /* queries; index is scanned whole */
    pvec  *PI = calloc(U_n, sizeof(pvec));
    pvec2 *PJ = calloc(U_n, sizeof(pvec2));
    int  *AB = malloc(U_n * sizeof(int));
    tvec *Q  = calloc(nq, sizeof(tvec));
    int  *AQ = malloc(nq * sizeof(int));
    long act_tot = 0, stored_p = 0, stored_p2 = 0, isect_tot = 0;
    for (int i = 0; i < U_n; i++) {
        AB[i] = t_active(&TI[i]); act_tot += AB[i];
        stored_p  += RD / 8 + (AB[i] + 7) / 8;
        stored_p2 += RD / 8 + RWORDS + (AB[i] + 7) / 8;
        pb_pack(&TI[i], &PI[i]); pb_pack2(&TI[i], &PJ[i]);
    }
    for (int j = 0; j < nq; j++) { t_encode(&R, V_t[j], &Q[j]); AQ[j] = t_active(&Q[j]); }
    double avg_act = (double)act_tot / U_n;
    double avg_p   = (double)stored_p  / U_n;
    double avg_p2  = (double)stored_p2 / U_n;

    printf("\n  === --packbench: packed sign plane vs the branchless bit-planes ===\n");
    printf("    index %d vectors x %d dev queries = %ld comparisons per pass\n",
           U_n, nq, (long)U_n * nq);
    printf("    active dims/vector mean %.1f of %d (%.1f%%)\n",
           avg_act, RD, 100.0 * avg_act / RD);
    printf("    stored B/vector: bit-planes %zu.0 | packed %.2f (%.3fx) | packed+rank %.2f (%.3fx)\n\n",
           sizeof(tvec), avg_p, (double)sizeof(tvec) / avg_p,
           avg_p2, (double)sizeof(tvec) / avg_p2);

    /* ---- EXACTNESS: full cross product, every query x every index vector ---- */
    long n = 0, bad_pack = 0, bad_ex = 0, bad_rk = 0, bad_rk2 = 0, bad_dot = 0;
    for (int j = 0; j < nq; j++)
        for (int i = 0; i < U_n; i++) {
            int s0  = t_score_pre(&Q[j], &TI[i], AQ[j], AB[i]);
            int d0  = t_dot(&Q[j], &TI[i]);
            int de  = pb_dot_expand(&Q[j], &PI[i]);
            int dr  = pb_dot_rank  (&Q[j], &PI[i]);
            int dr2 = pb_dot_rank2 (&Q[j], &PJ[i]);
            if (de != d0 || dr != d0 || dr2 != d0) bad_dot++;
            if (pb_score(de,  AQ[j], AB[i]) != s0) bad_ex++;
            if (pb_score(dr,  AQ[j], AB[i]) != s0) bad_rk++;
            if (pb_score(dr2, AQ[j], AB[i]) != s0) bad_rk2++;
            for (int w = 0; w < RWORDS; w++) isect_tot += pbpc(Q[j].m[w] & TI[i].m[w]);
            n++;
        }
    /* pack/unpack round-trip: the sign plane must be recoverable at every
       active dim (dims under a zero mask bit are unreadable BY CONSTRUCTION
       and are exactly the bytes being reclaimed). */
    for (int i = 0; i < U_n; i++) {
        int r = 0;
        for (int w = 0; w < RWORDS; w++) {
            uint32_t m = TI[i].m[w];
            while (m) {
                uint32_t low = m & (~m + 1u);
                int want = (TI[i].s[w] & low) ? 1 : 0;
                if (want != ((PI[i].sp[r >> 3] >> (r & 7)) & 1)) bad_pack++;
                if (want != ((PJ[i].sp[r >> 3] >> (r & 7)) & 1)) bad_pack++;
                r++; m ^= low;
            }
        }
    }
    printf("    EXACTNESS (full cross product, no sampling)\n");
    printf("      comparisons                  %ld\n", n);
    printf("      dot mismatches               %ld\n", bad_dot);
    printf("      score mismatches  EXPAND     %ld\n", bad_ex);
    printf("      score mismatches  RANK       %ld\n", bad_rk);
    printf("      score mismatches  RANK+      %ld\n", bad_rk2);
    printf("      pack round-trip sign errors  %ld  (over %ld active dims x2 forms)\n",
           bad_pack, act_tot);
    printf("      -> %s\n", (bad_dot | bad_ex | bad_rk | bad_rk2 | bad_pack)
                            ? "*** NOT BIT-EXACT ***" : "BIT-EXACT on all counts");
    printf("      mean mask intersection %.1f bits — the length of the rank loop\n\n",
           (double)isect_tot / n);

    /* ---- TIMING ---- */
    const int reps = 3;
    volatile long sink = 0;
    double t0, t1, ns[5];
    const char *nm[5] = {
        "t_score       (branchless, recomputes t_active)",
        "t_score_pre   (branchless, SHIPPED path)",
        "packed EXPAND (scatter signs, then branchless)",
        "packed RANK   (rank-index the intersection)",
        "packed RANK+  (prefix ranks precomputed, +8 B/vec)" };

    t0 = pb_ns();
    for (int r = 0; r < reps; r++) for (int j = 0; j < nq; j++) { long a = 0;
        for (int i = 0; i < U_n; i++) a += t_score(&Q[j], &TI[i], AQ[j]);
        sink += a; }
    t1 = pb_ns(); ns[0] = (t1 - t0) / ((double)reps * nq * U_n);

    t0 = pb_ns();
    for (int r = 0; r < reps; r++) for (int j = 0; j < nq; j++) { long a = 0;
        for (int i = 0; i < U_n; i++) a += t_score_pre(&Q[j], &TI[i], AQ[j], AB[i]);
        sink += a; }
    t1 = pb_ns(); ns[1] = (t1 - t0) / ((double)reps * nq * U_n);

    t0 = pb_ns();
    for (int r = 0; r < reps; r++) for (int j = 0; j < nq; j++) { long a = 0;
        for (int i = 0; i < U_n; i++) a += pb_score(pb_dot_expand(&Q[j], &PI[i]), AQ[j], AB[i]);
        sink += a; }
    t1 = pb_ns(); ns[2] = (t1 - t0) / ((double)reps * nq * U_n);

    t0 = pb_ns();
    for (int r = 0; r < reps; r++) for (int j = 0; j < nq; j++) { long a = 0;
        for (int i = 0; i < U_n; i++) a += pb_score(pb_dot_rank(&Q[j], &PI[i]), AQ[j], AB[i]);
        sink += a; }
    t1 = pb_ns(); ns[3] = (t1 - t0) / ((double)reps * nq * U_n);

    t0 = pb_ns();
    for (int r = 0; r < reps; r++) for (int j = 0; j < nq; j++) { long a = 0;
        for (int i = 0; i < U_n; i++) a += pb_score(pb_dot_rank2(&Q[j], &PJ[i]), AQ[j], AB[i]);
        sink += a; }
    t1 = pb_ns(); ns[4] = (t1 - t0) / ((double)reps * nq * U_n);

    printf("    HOST COST per vector scored (%d reps, %ld scores each)\n",
           reps, (long)nq * U_n);
    for (int i = 0; i < 5; i++)
        printf("      %-50s %8.2f ns   %6.2fx\n", nm[i], ns[i], ns[i] / ns[1]);
    printf("      (sink %ld — kept so nothing is optimised away)\n\n", (long)sink);

    /* ---- DOES IT PAY? ----------------------------------------------------
       Device numbers from the hardware-offload audit (doc/EXPERIMENTS.md):
         full scoring  flash-mapped 4168 ns/vec   DRAM 1617 ns/vec
         touch only    flash-mapped 1254 ns/vec   DRAM   63 ns/vec
       The memory penalty is the part that scales with BYTES; the compute is
       the part that scales with the ratio measured above. The flash penalty
       taken here is 4168-1617 = 2551 ns/vec (what flash residency costs over
       SRAM); the SRAM penalty is the 63 ns/vec touch cost itself. */
    const double FLASH_PEN = 2551.0;            /* ns/vector at 64 B */
    const double SRAM_PEN  =   63.0;            /* ns/vector at 64 B */
    const double DEV_COMPUTE = 1617.0 - 63.0;   /* ns/vector, memory removed */

    printf("    DOES IT PAY? (device model: compute %.0f ns/vec, memory scales with bytes)\n",
           DEV_COMPUTE);
    printf("      %-14s %7s %8s %10s %10s %10s %10s\n",
           "form", "B/vec", "ratio", "dCompute", "FLASH net", "SRAM net", "index KB");
    for (int i = 2; i < 5; i++) {
        double b = (i == 4) ? avg_p2 : avg_p;
        double shrink = b / (double)sizeof(tvec);
        double dc = DEV_COMPUTE * (ns[i] / ns[1] - 1.0);
        printf("      %-14s %7.2f %7.2fx %+9.0f %+10.0f %+10.0f %10.0f\n",
               i == 2 ? "EXPAND" : i == 3 ? "RANK" : "RANK+", b, ns[i] / ns[1], dc,
               dc - FLASH_PEN * (1.0 - shrink), dc - SRAM_PEN * (1.0 - shrink),
               U_n * b / 1024);
    }
    printf("      (negative net = PAYS; positive = COSTS)\n\n");
    printf("    BREAK-EVEN — the compute budget the byte saving buys:\n");
    for (int i = 2; i < 5; i++) {
        double b = (i == 4) ? avg_p2 : avg_p;
        double shrink = b / (double)sizeof(tvec);
        printf("      %-6s saves %4.1f B/vec -> budget %5.0f ns (flash) / %4.1f ns (SRAM)"
               "  = ratio x%.2f / x%.3f\n",
               i == 2 ? "EXPAND" : i == 3 ? "RANK" : "RANK+", sizeof(tvec) - b,
               FLASH_PEN * (1.0 - shrink), SRAM_PEN * (1.0 - shrink),
               1.0 + FLASH_PEN * (1.0 - shrink) / DEV_COMPUTE,
               1.0 + SRAM_PEN  * (1.0 - shrink) / DEV_COMPUTE);
    }
    { double cheap = ns[3] < ns[4] ? ns[3] : ns[4];
      double got  = cheap / ns[1] - 1.0;                       /* compute penalty */
      double bud  = FLASH_PEN * (1.0 - avg_p / (double)sizeof(tvec)) / DEV_COMPUTE;
      printf("      cheapest packed form measured x%.2f: penalty %.2f vs flash budget"
             " %.2f — OVER by %.1fx\n", cheap / ns[1], got, bud, got / bud); }
    printf("      index %.0f KB -> %.0f KB; SRAM has ~171 KB free, so packing alone\n"
           "      does NOT move the index into SRAM either.\n",
           U_n * (double)sizeof(tvec) / 1024, U_n * avg_p / 1024);
    free(PI); free(PJ); free(AB); free(Q); free(AQ);
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
        else if (!strcmp(a,"--packbench")) PACKBENCH=1;
        else if (!strcmp(a,"--footprint")) FOOTPRINT=1;
        else if (!strcmp(a,"--lmm-raw")) LMMRAW=1;
        else if (!strcmp(a,"--lmm-raw3")) LMMRAW3=1;
        else if (!strcmp(a,"--asym")) ASYM=1;
        else if (!strcmp(a,"--gatecheck")) GATECHK=1;
        else if (!strcmp(a,"--gatesel")) { USEGATE=1; PRIORCLS=1; }
        else if (!strcmp(a,"--selsig")) { SELSIG=1; SELMARG=8; }
        else if (!strcmp(a,"--priorcls")) PRIORCLS=1;
        else if (!strcmp(a,"--priorcls2")) PRIORCLS=2;
        else if (!strncmp(a,"--selmargin=",12)) SELMARG=atoi(a+12);
        else if (!strcmp(a,"--rankoracle")) RANKORACLE=1;
        else if (!strcmp(a,"--rerankoracle")) RERANK=1;
        else if (!strcmp(a,"--condcentre")) CONDCENTRE=1;
        else if (!strcmp(a,"--abstain")) ABSTAIN=1;
        else if (!strcmp(a,"--faprobe")) FAPROBE=1;
        else if (!strcmp(a,"--contrast")) CONTRAST=1;
        else if (!strcmp(a,"--coverage")) COVERAGE=1;
        else if (!strcmp(a,"--corrob")) CORROB=1;
        else if (!strcmp(a,"--dumpdisp")) DUMPDISP=1;
        else if (!strcmp(a,"--prune-diag")) PRUNEDIAG=1;
        else if (!strcmp(a,"--ship")) { FIXTH=RSHIP_TH; PRUNE.neg_top=RSHIP_NEGTOP; }
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
    int64_t nz[RD]; memset(nz,0,sizeof nz);
    for(int i=0;i<U_n;i++){ r_counts(U_t[i],acc,&tot);
        for(int d=0;d<RD;d++){ sum[d]+=((int64_t)acc[d]*RSCALE)/tot; if(acc[d]) nz[d]++; } }
    /* CONDCENTRE: divide by the number of vectors in which the dim actually
       FIRED, not by every vector. t_encode consults centre[] only for dims with
       acc!=0, so a mean diluted by the ~77%% of vectors where the dim is zero is
       a bar that any firing dim clears - measured, the sign plane comes out
       99.3%% ones, i.e. 32 bytes/vector carrying ~0.06 bits per active dim. */
    for(int d=0;d<RD;d++) R.centre[d]=(int32_t)(sum[d]/(CONDCENTRE?(nz[d]?nz[d]:1):U_n));
    R.index=calloc(U_n,sizeof(rvec)); R.label=calloc(U_n,1); TI=calloc(U_n,sizeof(tvec));
    for(int i=0;i<U_n;i++){ r_encode(&R,U_t[i],&R.index[i]); t_encode(&R,U_t[i],&TI[i]);
        for(uint32_t c=0;c<R.n_class;c++) if(!strcmp(R.names[c],U_l[i])){R.label[i]=c;break;} }
    /* Pruning runs AFTER encoding with the full-index centre, so every surviving
       code is bit-identical to the unpruned run: this isolates "fewer stored
       vectors" from "different encoder". Shared with mkblob (see prune.h). */
    /* verbose: 0 for the single-query user tools (route/repl) - their output is
       quoted in the README and must be the answer, nothing else; 2 only when
       --prune-diag asks for the guard-coverage table, which is an internal
       diagnostic and was leaking into `make demo` and `make route`. */
    U_n = prune_index(U_t, U_l, &R, TI, R.index, U_n, PRUNE,
                      (ROUTE1 || REPL) ? 0 : (PRUNEDIAG ? 2 : 1));
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
    if(PACKBENCH){ packbench(); return 0; }
    if(RANKORACLE){ rankoracle(); return 0; }
    if(RERANK){ rerankoracle(); return 0; }
    if(ABSTAIN){ abstain(); return 0; }
    if(FAPROBE){ faprobe(); return 0; }
    if(CONTRAST){ contrast(); return 0; }
    if(COVERAGE){ coverage(); return 0; }
    if(CORROB){ corrob(); return 0; }
    if(DUMPDISP){ dumpdisp(); return 0; }
    if(FOOTPRINT){ footprint(); return 0; }
    if(LMMRAW){ lmm_raw(); return 0; }
    if(LMMRAW3){ lmm_raw3(); return 0; }
    if(ASYM){ asym(); return 0; }
    if(GATECHK){ gatecheck(); return 0; }
    if(SELSIG){ selsig(); return 0; }
    report("binary (1 bit)",   score_bin,-RD,RD,          U_n*sizeof(rvec)/1024.0);
    report("twin-ternary (2b)",score_ter,-512,512,        U_n*sizeof(tvec)/1024.0);
    /* word prior CUT: passes breaks-zero but does not move the operating curve.
       Reproduce with --curve. Code retained in prior.c, not in the router path. */
    (void)score_cas; (void)tb;   /* cascade measured: identical on every axis, cut */
    return 0;
}
