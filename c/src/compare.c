/* compare.c — binary vs twin-ternary vs cascade. Integer only.
 * Each router scores a query ONCE; thresholds sweep cached scores. */
#include "ternary.h"
#include "cascade.h"
#include "invariants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAXU 40000
static char *U_t[MAXU]; static char U_l[MAXU][RNAMELEN]; static int U_n;
static char *V_t[3000]; static char V_l[3000][RNAMELEN]; static int V_n;
static char *T_t[4000]; static char T_l[4000][RNAMELEN]; static int T_n;
static router_t R; static tvec *TI; static tvec TSIG[RMAXCLS];
static int SIGMODE = 0;  /* re-open: mask choice also came from leaked dev */
static int NOVETO  = 0;  /* re-open: the OFF decision came from leaked dev */
static int LEAKTEST = 0; /* --leak: reintroduce the bug on purpose, to verify the guard */

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
    int sb=-1<<28,sc=-1;
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
static hit score_ter(const char*txt){
    tvec q; t_encode(&R,txt,&q); int aa=t_active(&q);
    int best=-1<<28; uint32_t bi=0;
    for(uint32_t i=0;i<R.n_index;i++){int s=t_score(&q,&TI[i],aa); if(s>best){best=s;bi=i;}}
    hit h={best, veto(&q,aa,r_apply_polarity(&R,R.label[bi],txt))}; return h;
}
/* stage 1 REORDERS (counting sort on mask overlap), never rejects.
 * stage 2 reranks the survivors against the stored text. */
static hit score_cas(const char*txt){
    tvec q; t_encode(&R,txt,&q); int aa=t_active(&q);
    static int ov[MAXU]; int cnt[RD+2]; memset(cnt,0,sizeof cnt);
    for(uint32_t i=0;i<R.n_index;i++){ ov[i]=c_mask_overlap(&q,&TI[i]); cnt[ov[i]]++; }
    int acc=0,cut=RD;
    for(; cut>=0; cut--){ acc+=cnt[cut]; if(acc>=CAS_K) break; }
    int best=-1<<28,bi=-1;
    for(uint32_t i=0;i<R.n_index;i++){
        if(ov[i]<cut) continue;   /* true cut, no index-order truncation */
        int s=t_score(&q,&TI[i],aa)+c_text_sim(txt,U_t[i]);
        if(s>best){best=s;bi=(int)i;}
    }
    if(bi<0){ hit h={-1<<28,-1}; return h; }
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
static void report(const char *name, hit (*f)(const char *), int lo, int hi, double kb) {
    hit *hv = precompute(f, V_t, V_n);
    int th = tune(hv, V_l, V_n, lo, hi);
    hit *hh = USE_TEST ? precompute(f, T_t, T_n) : hv;
    char (*lab)[RNAMELEN] = USE_TEST ? T_l : V_l;
    int n = USE_TEST ? T_n : V_n;
    TX z = tally(hh, th, lab, n);
    double p = z.in ? (double)z.iok / z.in : 0.0;
    double se = 100.0 * sqrt(p * (1 - p) / (z.in ? z.in : 1));
    printf("  %-20s %6.1f%% +-%.1f  %-7d %-7d %7.0f  th=%d\n",
           name, 100.0 * p, se, z.fa + z.wa, z.ms, kb, th);
    if (LAST) mcnemar(LAST, LAST_TH, hh, th, lab, n);
    LAST = hh; LAST_TH = th; LAST_NAME = name;
}
int main(int argc,char**argv){
    if(argc<5){fprintf(stderr,"usage: compare train val test nlu.csv [--test]\n");return 1;}
    for(int i=5;i<argc;i++){ if(!strcmp(argv[i],"--test")) USE_TEST=1;
        else if(!strncmp(argv[i],"--sig=",6)) SIGMODE=atoi(argv[i]+6);
        else if(!strcmp(argv[i],"--noveto")) NOVETO=1;
        else if(!strcmp(argv[i],"--leak")) LEAKTEST=1; }
    if(USE_TEST){
        FILE *bf=fopen("results/TEST_BUDGET","r"); int used=0;
        if(bf){ if(fscanf(bf,"%d",&used)!=1) used=0; fclose(bf); }
        used++;
        bf=fopen("results/TEST_BUDGET","w"); if(bf){ fprintf(bf,"%d\n",used); fclose(bf); }
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
    long tb=0; for(int i=0;i<U_n;i++) tb+=strlen(U_t[i])+1;
    printf("\n  %-20s %8s %-8s %-8s %8s\n","representation","iot acc","wrong","missed","index KB");
    report("binary (1 bit)",   score_bin,-RD,RD,          U_n*sizeof(rvec)/1024.0);
    report("twin-ternary (2b)",score_ter,-512,512,        U_n*sizeof(tvec)/1024.0);
    (void)score_cas; (void)tb;   /* cascade measured: identical on every axis, cut */
    return 0;
}
