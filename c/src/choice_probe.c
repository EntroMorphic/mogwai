/* choice_probe.c -- runtime-choice probe for graph/topology experiments.
 *
 * This deliberately does NOT add a learned encoder or NSW yet. It answers the
 * first P0 question: before adding weights, does an arbitrary query and an
 * arbitrary candidate description land in the same exact top-k neighborhood of
 * the existing Mogwai exemplar manifold?
 */
#include "ternary.h"
#include "prune.h"
#include "invariants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXU 40000
#define MAXC 256
#define MAXK 64
#define COLLISION_WARN 180
#define MARGIN_WARN 20

typedef struct { int idx, score; } near_t;
typedef struct {
    int best, best_score, second, margin, coll_i, coll_j, coll_best;
    int best_direct, best_overlap, best_hist, best_reachable;
    int direct[MAXC], overlap[MAXC], hist[MAXC], combo[MAXC], top_cls[MAXC];
    near_t qn[MAXK], cn[MAXC][MAXK];
} probe_result;

static char *U_t[MAXU]; static char U_l[MAXU][RNAMELEN]; static int U_n;
static char *V_t[3000]; static char V_l[3000][RNAMELEN]; static int V_n;
static char *T_t[4000]; static int T_n;
static router_t R; static tvec *TI; static uint16_t *ACT;
static prune_opt PRUNE = {0,0,0,0,RSHIP_NEGBOUND,0};

static char *xstrdup(const char *s) {
    char *p = strdup(s);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(1); }
    return p;
}

static int js(const char*l,const char*k,char*o,int cap){
    char pat[64]; snprintf(pat,sizeof pat,"\"%s\":",k);
    const char*p=strstr(l,pat); if(!p)return 0; p+=strlen(pat);
    while (*p == ' ') p++;
    if (*p != '"') return 0;
    p++;
    int n=0; while(*p&&*p!='"'&&n<cap-1){if(*p=='\\'&&p[1])p++;o[n++]=*p++;} o[n]=0; return 1; }
static int isiot(const char*l){return !strncmp(l,"iot_",4);}

#define HN 65536
static char *HS[HN];
static void hs_add(const char*s){ char b[512]; r_norm(s,b,sizeof b);
    uint32_t h=r_fnv(b,(int)strlen(b))%HN;
    for(int n=0; HS[h] && n<HN; n++, h=(h+1)%HN) if(!strcmp(HS[h],b))return;
    if(HS[h]){ fprintf(stderr,"hash set full\n"); exit(1); }
    HS[h]=xstrdup(b); }
static int hs_has(const char*s){ char b[512]; r_norm(s,b,sizeof b);
    uint32_t h=r_fnv(b,(int)strlen(b))%HN;
    for(int n=0; HS[h] && n<HN; n++, h=(h+1)%HN) if(!strcmp(HS[h],b))return 1;
    return 0; }
static void push(char**ta,char la[][RNAMELEN],int*n,int cap,const char*t,const char*l){
    if(*n>=cap){ fprintf(stderr,"too many utterances (max %d)\n",cap); exit(1); }
    ta[*n]=xstrdup(t); if(la) snprintf(la[*n],RNAMELEN,"%s",l); (*n)++; }

static int class_id(const char *l) {
    for (uint32_t c = 0; c < R.n_class; c++) if (!strcmp(R.names[c], l)) return (int)c;
    if (R.n_class >= RMAXCLS) { fprintf(stderr, "too many classes\n"); exit(1); }
    snprintf(R.names[R.n_class], RNAMELEN, "%.*s", RNAMELEN - 1, l);
    return (int)R.n_class++;
}

static void usage(void) {
    fprintf(stderr,
        "usage: choice_probe [train validation test nlu.csv] --query TEXT --choice TEXT [--choice TEXT ...] [--k=N]\n"
        "       choice_probe [train validation test nlu.csv] --demo [--k=N]\n"
        "       choice_probe [train validation test nlu.csv] --redteam\n"
        "\n"
        "Builds the shipped Mogwai index, then compares arbitrary runtime choice\n"
        "descriptions by direct twin-ternary score and exact top-k neighborhood\n"
        "signatures over the existing exemplar corpus. DIAGNOSTIC ONLY: combo is\n"
        "not a production decision rule. No test-set evaluation.\n");
}

static void load_data(const char *train, const char *val, const char *test, const char *nlu) {
    char line[8192], t[512], l[RNAMELEN]; FILE *f;
    f=fopen(test,"r"); if(!f){perror(test);exit(1);} while(fgets(line,sizeof line,f))
        if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l)) { push(T_t,NULL,&T_n,4000,t,NULL); hs_add(t); }
    fclose(f);

    f=fopen(train,"r"); if(!f){perror(train);exit(1);} int ti=0,tn=0;
    while(fgets(line,sizeof line,f)) if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l)){
        int io=isiot(l);
        if(hs_has(t)) continue;
        if(io&&(ti++%4)==0){ push(V_t,V_l,&V_n,3000,t,l); hs_add(t); continue; }
        if(!io&&(tn++%8)==0){ push(V_t,V_l,&V_n,3000,t,"none"); hs_add(t); continue; }
        push(U_t,U_l,&U_n,MAXU,t,io?l:"none"); hs_add(t);
    }
    fclose(f);

    f=fopen(val,"r"); if(!f){perror(val);exit(1);} while(fgets(line,sizeof line,f))
        if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l))
            if(isiot(l)&&!hs_has(t)){ push(U_t,U_l,&U_n,MAXU,t,l); hs_add(t); }
    fclose(f);

    f=fopen(nlu,"r"); if(!f){perror(nlu);exit(1);} while(fgets(line,sizeof line,f)){
        char*fl[12]={0}; int nf=0,inq=0; char*p=line; fl[nf++]=p;
        for(;*p&&nf<12;p++){ if(*p=='"')inq=!inq; else if(*p==';'&&!inq){*p=0;fl[nf++]=p+1;} }
        if(nf<10)continue;
        for(int i=0;i<nf;i++){char*s=fl[i];int L=(int)strlen(s);
            while(L&&(s[L-1]=='\n'||s[L-1]=='\r'))s[--L]=0;
            if(L>=2&&s[0]=='"'&&s[L-1]=='"'){s[L-1]=0;fl[i]=s+1;} }
        if(strcmp(fl[2],"iot")||hs_has(fl[9]))continue;
        char lb[RNAMELEN]; snprintf(lb,sizeof lb,"iot_%s",fl[3]);
        push(U_t,U_l,&U_n,MAXU,fl[9],lb); hs_add(fl[9]);
    }
    fclose(f);

    inv_disjoint("index vs DEV",  U_t,U_n,V_t,V_n);
    inv_disjoint("index vs TEST", U_t,U_n,T_t,T_n);

    memset(&R,0,sizeof R); R.magic=RMAGIC; R.dim=RD; R.n_index=U_n; R.threshold=RSHIP_TH;
    for(int i=0;i<U_n;i++) (void)class_id(U_l[i]);
    int64_t sum[RD]; memset(sum,0,sizeof sum); int16_t acc[RD]; int32_t tot;
    for(int i=0;i<U_n;i++){ r_counts(U_t[i],acc,&tot);
        for(int d=0;d<RD;d++) sum[d]+=((int64_t)acc[d]*RSCALE)/tot; }
    for(int d=0;d<RD;d++) R.centre[d]=(int32_t)(sum[d]/U_n);
    R.label=calloc((size_t)U_n,1); TI=calloc((size_t)U_n,sizeof *TI);
    if(!R.label || !TI){fprintf(stderr,"out of memory\n");exit(1);}
    for(int i=0;i<U_n;i++){ t_encode(&R,U_t[i],&TI[i]); R.label[i]=(uint8_t)class_id(U_l[i]); }
    U_n = prune_index(U_t,U_l,&R,TI,NULL,U_n,PRUNE,0);
    R.n_index = (uint32_t)U_n;
    ACT=calloc((size_t)U_n,sizeof *ACT); if(!ACT){fprintf(stderr,"out of memory\n");exit(1);}
    for(int i=0;i<U_n;i++) ACT[i]=(uint16_t)t_active(&TI[i]);
}

static int cmp_near(const void *a, const void *b) {
    const near_t *x=a, *y=b;
    if (x->score != y->score) return y->score - x->score;
    return x->idx - y->idx;
}

static void topk(const char *text, near_t *out, int k, tvec *vec_out, int *act_out) {
    tvec q; t_encode(&R,text,&q); int aa=t_active(&q);
    near_t *all=malloc((size_t)U_n*sizeof *all); if(!all){fprintf(stderr,"out of memory\n");exit(1);}
    for(int i=0;i<U_n;i++){ all[i].idx=i; all[i].score=t_score_pre(&q,&TI[i],aa,ACT[i]); }
    qsort(all,(size_t)U_n,sizeof *all,cmp_near);
    for(int i=0;i<k;i++) out[i]=all[i];
    free(all);
    if(vec_out) *vec_out=q;
    if(act_out) *act_out=aa;
}

static int direct(const tvec *a, int aa, const tvec *b, int ab) {
    return t_score_pre(a,b,aa,ab);
}

static int overlap(const near_t *a, const near_t *b, int k) {
    int n=0;
    for(int i=0;i<k;i++) for(int j=0;j<k;j++) if(a[i].idx==b[j].idx) n++;
    return n;
}

static void hist(const near_t *n, int k, int *h) {
    memset(h,0,RMAXCLS*sizeof *h);
    for(int i=0;i<k;i++) h[R.label[n[i].idx]] += k - i;
}

static int hist_dot(const int *a, const int *b) {
    int d=0, aa=0, bb=0;
    for(uint32_t c=0;c<R.n_class;c++){ d+=a[c]*b[c]; aa+=a[c]*a[c]; bb+=b[c]*b[c]; }
    if(!aa || !bb) return 0;
    /* Integer-friendly bounded proxy, not a cosine: 1000 means identical mass. */
    return (2000*d)/(aa+bb);
}

static void print_neighbors(const char *tag, const near_t *n, int k) {
    printf("  %s nearest:\n", tag);
    int show = k < 5 ? k : 5;
    for(int i=0;i<show;i++) printf("    %4d  %-20s  \"%s\"\n", n[i].score, R.names[R.label[n[i].idx]], U_t[n[i].idx]);
}

static probe_result eval_probe(const char *query, const char **choices, int nc, int k) {
    probe_result r; memset(&r,0,sizeof r);
    tvec qv, cv[MAXC]; int qa, ca[MAXC]; int qh[RMAXCLS], ch[MAXC][RMAXCLS];
    topk(query, r.qn, k, &qv, &qa); hist(r.qn,k,qh);
    r.best=-1; r.best_score=-(1<<28); r.second=-(1<<28);
    for(int i=0;i<nc;i++){
        topk(choices[i], r.cn[i], k, &cv[i], &ca[i]); hist(r.cn[i],k,ch[i]);
        r.direct[i]=direct(&qv,qa,&cv[i],ca[i]);
        r.overlap[i]=overlap(r.qn,r.cn[i],k);
        r.hist[i]=hist_dot(qh,ch[i]);
        /* Diagnostic score, not a decision rule: direct remains primary, while
           topology shows whether both strings landed in the same exemplar basin. */
        r.combo[i] = r.direct[i] + 4*r.overlap[i] + r.hist[i]/10;
        r.top_cls[i] = R.label[r.cn[i][0].idx];
        if(r.combo[i]>r.best_score){r.second=r.best_score;r.best_score=r.combo[i];r.best=i;}
        else if(r.combo[i]>r.second) r.second=r.combo[i];
    }
    r.coll_i=-1; r.coll_j=-1; r.coll_best=-(1<<28);
    for(int i=0;i<nc;i++) for(int j=i+1;j<nc;j++){
        int ds=direct(&cv[i],ca[i],&cv[j],ca[j]);
        if(ds>r.coll_best){r.coll_best=ds;r.coll_i=i;r.coll_j=j;}
    }
    r.margin = r.best_score - r.second;
    r.best_direct = r.direct[r.best];
    r.best_overlap = r.overlap[r.best];
    r.best_hist = r.hist[r.best];
    r.best_reachable = r.best_overlap > 0 || r.best_hist >= 500;
    return r;
}

static void run_probe(const char *query, const char **choices, int nc, int k) {
    probe_result r = eval_probe(query,choices,nc,k);
    printf("index %d vectors, %u classes, k=%d, threshold=%d\n", U_n, R.n_class, k, R.threshold);
    printf("DIAGNOSTIC ONLY: combo is a research feature mix, not a production choice rule.\n");
    printf("\nquery: \"%s\"\n", query);
    print_neighbors("query", r.qn, k);
    printf("\nchoices:\n");
    for(int i=0;i<nc;i++){
        printf("  [%d] combo=%4d direct=%4d overlap=%2d/%d hist=%4d top=%-20s  \"%s\"\n",
               i, r.combo[i], r.direct[i], r.overlap[i], k, r.hist[i],
               R.names[r.top_cls[i]], choices[i]);
        print_neighbors("choice", r.cn[i], k);
    }
    printf("\nresult: choice=%d combo=%d margin=%d candidate_pair_max=%d (%d,%d)\n",
           r.best,r.best_score,r.margin,r.coll_best,r.coll_i,r.coll_j);
    printf("metrics: direct=%d neighborhood_overlap=%d/%d neighborhood_agreement=%d margin=%d reachable=%s\n",
           r.best_direct,r.best_overlap,k,r.best_hist,r.margin,r.best_reachable?"yes":"no");
    if(r.margin < MARGIN_WARN) printf("  WARN low_margin: %d < %d\n", r.margin, MARGIN_WARN);
    if(r.coll_best >= COLLISION_WARN) printf("  WARN candidate_collision: %d >= %d\n", r.coll_best, COLLISION_WARN);
    if(!strcmp(R.names[r.top_cls[r.best]],"none")) printf("  WARN none_basin: winning choice lands nearest to none exemplars\n");
}

static int rt_total, rt_pass;
static void rt_chk(const char *name, int ok) {
    rt_total++;
    if(ok) rt_pass++;
    else printf("  FAIL redteam: %s\n", name);
}

static int run_redteam(void) {
    const char *demo[] = {
        "increase the brightness of the bedroom lights",
        "decrease the brightness of the bedroom lights",
        "turn the bedroom lights completely off"};
    probe_result r = eval_probe("make the bedroom darker", demo, 3, 8);
    rt_chk("demo keeps dim candidate on top", r.best == 1);
    rt_chk("demo reports direct similarity", r.best_direct == r.direct[r.best]);
    rt_chk("demo reports neighborhood agreement", r.best_hist == r.hist[r.best]);
    rt_chk("demo reports reachability", r.best_reachable == (r.best_overlap > 0 || r.best_hist >= 500));
    rt_chk("demo exposes low margin", r.margin < MARGIN_WARN);
    rt_chk("demo exposes brightness collision", r.coll_best >= COLLISION_WARN);
    rt_chk("demo direct scores are close", abs(r.direct[1] - r.direct[0]) <= 2);

    const char *off[] = {
        "turn the kitchen lights on", "turn the kitchen lights off", "make the kitchen brighter"};
    r = eval_probe("turn the kitchen lights off", off, 3, 8);
    rt_chk("off case chooses off", r.best == 1);
    rt_chk("off correct answer reachable", r.best_reachable);
    rt_chk("off case has useful margin", r.margin >= MARGIN_WARN);
    rt_chk("off exact paraphrase overlaps all neighbors", r.overlap[1] == 8);
    rt_chk("off/on collision is visible", r.coll_best >= COLLISION_WARN);

    const char *dim[] = {"lower brightness", "decrease brightness", "make darker"};
    r = eval_probe("dim lights", dim, 3, 8);
    rt_chk("topology rescue chooses lower brightness", r.best == 0);
    rt_chk("topology rescue marks reachable", r.best_reachable);
    rt_chk("topology rescue is not direct-score driven", r.direct[0] < r.direct[1]);
    rt_chk("topology rescue has strong dim histogram", r.hist[0] > 900);
    rt_chk("topology rescue flags candidate closeness", r.coll_best >= 140);

    const char *lum[] = {
        "lower the brightness of the lights", "raise the brightness of the lights", "turn the coffee machine on"};
    r = eval_probe("make the room less luminous", lum, 3, 8);
    rt_chk("less-luminous known failure is exposed", r.best != 0);
    rt_chk("less-luminous reports low reachability or low margin", !r.best_reachable || r.margin < MARGIN_WARN);
    rt_chk("less-luminous failure has low margin", r.margin < MARGIN_WARN);
    rt_chk("less-luminous lower/raise collision visible", r.coll_best >= COLLISION_WARN);
    rt_chk("less-luminous rejects coffee neighborhood", r.combo[2] + 40 < r.combo[r.best]);

    const char *refund[] = {
        "issue money back to the customer", "deny the refund request", "escalate to technical support"};
    r = eval_probe("refund the customer", refund, 3, 8);
    rt_chk("out-of-domain refund chooses closest paraphrase", r.best == 0);
    rt_chk("out-of-domain refund reachability is none-basin", r.best_reachable);
    rt_chk("out-of-domain refund lands in none basin", !strcmp(R.names[r.top_cls[r.best]],"none"));
    rt_chk("out-of-domain none histogram dominates", r.hist[0] > 900);
    rt_chk("out-of-domain unrelated technical stays behind", r.combo[2] + 100 < r.combo[0]);

    printf("REDTEAM checks=%d/%d score=%d/100\n", rt_pass, rt_total,
           rt_total ? (100 * rt_pass) / rt_total : 0);
    return rt_pass == rt_total ? 0 : 1;
}

int main(int argc, char **argv) {
    const char *paths[4]={"data/train.json","data/validation.json","data/test.json","data/nlu_home.csv"};
    int arg=1, k=8, demo=0, redteam=0; const char *query=NULL; const char *choices[MAXC]; int nc=0;
    if(argc>=5 && argv[1][0]!='-' && argv[2][0]!='-' && argv[3][0]!='-' && argv[4][0]!='-'){
        for(int i=0;i<4;i++) paths[i]=argv[1+i];
        arg=5;
    }
    for(int i=arg;i<argc;i++){
        if(!strcmp(argv[i],"--demo")) demo=1;
        else if(!strcmp(argv[i],"--redteam")) redteam=1;
        else if(!strcmp(argv[i],"--query") && i+1<argc) query=argv[++i];
        else if(!strcmp(argv[i],"--choice") && i+1<argc){ if(nc>=MAXC){fprintf(stderr,"too many choices\n");return 1;} choices[nc++]=argv[++i]; }
        else if(!strncmp(argv[i],"--k=",4)) k=atoi(argv[i]+4);
        else { usage(); return 1; }
    }
    if(k<1 || k>MAXK){ fprintf(stderr,"--k must be 1..%d\n",MAXK); return 1; }
    if(demo && (query || nc)){ fprintf(stderr,"--demo cannot be mixed with --query/--choice\n"); return 1; }
    if(redteam && (demo || query || nc)){ fprintf(stderr,"--redteam cannot be mixed with probes\n"); return 1; }
    if(demo){
        query="make the bedroom darker";
        choices[0]="increase the brightness of the bedroom lights";
        choices[1]="decrease the brightness of the bedroom lights";
        choices[2]="turn the bedroom lights completely off";
        nc=3;
    }
    if(!redteam && (!query || nc<2)){ usage(); return 1; }
    load_data(paths[0],paths[1],paths[2],paths[3]);
    if(redteam) return run_redteam();
    run_probe(query,choices,nc,k);
    return 0;
}
