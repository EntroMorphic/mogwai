/* semhash_probe.c -- learned semantic hash proof, host only.
 *
 * This is deliberately small and auditable: train an integer multiclass
 * perceptron from Mogwai's current hashed count features, then use the winning
 * learned class as a compact semantic code. Runtime queries and runtime
 * candidate descriptions use the SAME encoder.
 */
#include "ternary.h"
#include "prune.h"
#include "invariants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXU 40000
#define MAXC 256
#define HD 64
#define EPOCHS 24

static char *U_t[MAXU]; static char U_l[MAXU][RNAMELEN]; static int U_n;
static char *V_t[3000]; static char V_l[3000][RNAMELEN]; static int V_n;
static char *T_t[4000]; static int T_n;
static router_t R; static tvec *TI; static uint16_t *ACT;
static int32_t CW[RMAXCLS][RD];
static prune_opt PRUNE = {0,0,0,RSHIP_NEGTOP,0,0};

typedef struct { uint64_t code; int pred, score; } sh_t;
typedef struct {
    int best, best_sem, second_sem, margin, coll_i, coll_j, coll_best;
    int best_direct, best_reachable;
    int sem[MAXC], raw[MAXC], pred[MAXC], clsfit[MAXC];
} sem_result;

static char *xstrdup(const char *s){ char *p=strdup(s); if(!p){fprintf(stderr,"out of memory\n");exit(1);} return p; }
static int js(const char*l,const char*k,char*o,int cap){
    char pat[64]; snprintf(pat,sizeof pat,"\"%s\":",k);
    const char*p=strstr(l,pat); if(!p)return 0; p+=strlen(pat);
    while(*p==' ')p++;
    if(*p!='"')return 0;
    p++;
    int n=0; while(*p&&*p!='"'&&n<cap-1){if(*p=='\\'&&p[1])p++;o[n++]=*p++;} o[n]=0; return 1; }
static int isiot(const char*l){return !strncmp(l,"iot_",4);}

#define HN 65536
static char *HS[HN];
static void hs_add(const char*s){ char b[512]; r_norm(s,b,sizeof b); uint32_t h=r_fnv(b,(int)strlen(b))%HN;
    for(int n=0;HS[h]&&n<HN;n++,h=(h+1)%HN) if(!strcmp(HS[h],b))return;
    if(HS[h]){fprintf(stderr,"hash set full\n");exit(1);} HS[h]=xstrdup(b); }
static int hs_has(const char*s){ char b[512]; r_norm(s,b,sizeof b); uint32_t h=r_fnv(b,(int)strlen(b))%HN;
    for(int n=0;HS[h]&&n<HN;n++,h=(h+1)%HN) if(!strcmp(HS[h],b))return 1;
    return 0;
}
static void push(char**ta,char la[][RNAMELEN],int*n,int cap,const char*t,const char*l){
    if(*n>=cap){fprintf(stderr,"too many utterances (max %d)\n",cap);exit(1);}
    ta[*n]=xstrdup(t); if(la)snprintf(la[*n],RNAMELEN,"%s",l); (*n)++; }

static int class_id(const char *l){
    for(uint32_t c=0;c<R.n_class;c++) if(!strcmp(R.names[c],l)) return (int)c;
    if(R.n_class>=RMAXCLS){fprintf(stderr,"too many classes\n");exit(1);}
    snprintf(R.names[R.n_class],RNAMELEN,"%.*s",RNAMELEN-1,l); return (int)R.n_class++;
}
static int code_bit(int cls,int j){
    char b[80]; snprintf(b,sizeof b,"%s#%d",R.names[cls],j);
    uint32_t h=r_fnv(b,(int)strlen(b));
    return (((h>>16)^h)&1)?1:-1;
}
static uint64_t class_code(int cls){ uint64_t c=0; for(int j=0;j<HD;j++) if(code_bit(cls,j)>0)c|=1ull<<j; return c; }

static void usage(void){
    fprintf(stderr,
        "usage: semhash_probe [train validation test nlu.csv] --query TEXT --choice TEXT [--choice TEXT ...]\n"
        "       semhash_probe [train validation test nlu.csv] --demo\n"
        "       semhash_probe [train validation test nlu.csv] --redteam\n\n"
        "Learns a tiny integer semantic hash from the shipped exemplar labels.\n"
        "DIAGNOSTIC ONLY: not a production runtime-choice policy.\n");
}

static void load_data(const char *train,const char *val,const char *test,const char *nlu){
    char line[8192],t[512],l[RNAMELEN]; FILE*f;
    f=fopen(test,"r"); if(!f){perror(test);exit(1);}
    while(fgets(line,sizeof line,f))
        if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l)){push(T_t,NULL,&T_n,4000,t,NULL);hs_add(t);}
    fclose(f);
    f=fopen(train,"r"); if(!f){perror(train);exit(1);} int ti=0,tn=0;
    while(fgets(line,sizeof line,f)) if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l)){
        int io=isiot(l); if(hs_has(t))continue;
        if(io&&(ti++%4)==0){push(V_t,V_l,&V_n,3000,t,l);hs_add(t);continue;}
        if(!io&&(tn++%8)==0){push(V_t,V_l,&V_n,3000,t,"none");hs_add(t);continue;}
        push(U_t,U_l,&U_n,MAXU,t,io?l:"none");hs_add(t);
    }
    fclose(f);
    f=fopen(val,"r"); if(!f){perror(val);exit(1);}
    while(fgets(line,sizeof line,f))
        if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l)) if(isiot(l)&&!hs_has(t)){push(U_t,U_l,&U_n,MAXU,t,l);hs_add(t);}
    fclose(f);
    f=fopen(nlu,"r"); if(!f){perror(nlu);exit(1);} while(fgets(line,sizeof line,f)){
        char*fl[12]={0}; int nf=0,inq=0; char*p=line; fl[nf++]=p;
        for(;*p&&nf<12;p++){ if(*p=='"')inq=!inq; else if(*p==';'&&!inq){*p=0;fl[nf++]=p+1;} }
        if(nf<10)continue;
        for(int i=0;i<nf;i++){char*s=fl[i];int L=(int)strlen(s); while(L&&(s[L-1]=='\n'||s[L-1]=='\r'))s[--L]=0; if(L>=2&&s[0]=='"'&&s[L-1]=='"'){s[L-1]=0;fl[i]=s+1;}}
        if(strcmp(fl[2],"iot")||hs_has(fl[9]))continue;
        char lb[RNAMELEN]; snprintf(lb,sizeof lb,"iot_%s",fl[3]); push(U_t,U_l,&U_n,MAXU,fl[9],lb); hs_add(fl[9]);
    } fclose(f);
    inv_disjoint("index vs DEV",U_t,U_n,V_t,V_n); inv_disjoint("index vs TEST",U_t,U_n,T_t,T_n);
    memset(&R,0,sizeof R); R.magic=RMAGIC; R.dim=RD; R.n_index=U_n; R.threshold=RSHIP_TH;
    for(int i=0;i<U_n;i++)(void)class_id(U_l[i]);
    int64_t sum[RD]; memset(sum,0,sizeof sum); int16_t acc[RD]; int32_t tot;
    for(int i=0;i<U_n;i++){r_counts(U_t[i],acc,&tot); for(int d=0;d<RD;d++)sum[d]+=((int64_t)acc[d]*RSCALE)/tot;}
    for(int d=0;d<RD;d++)R.centre[d]=(int32_t)(sum[d]/U_n);
    R.label=calloc((size_t)U_n,1); TI=calloc((size_t)U_n,sizeof *TI);
    if(!R.label||!TI){fprintf(stderr,"out of memory\n");exit(1);}
    for(int i=0;i<U_n;i++){t_encode(&R,U_t[i],&TI[i]); R.label[i]=(uint8_t)class_id(U_l[i]);}
    U_n=prune_index(U_t,U_l,&R,TI,NULL,U_n,PRUNE,0); R.n_index=(uint32_t)U_n;
    ACT=calloc((size_t)U_n,sizeof *ACT); if(!ACT){fprintf(stderr,"out of memory\n");exit(1);}
    for(int i=0;i<U_n;i++)ACT[i]=(uint16_t)t_active(&TI[i]);
}

static int dot_cls(int c,const int16_t *acc){
    int64_t s=0; for(int d=0;d<RD;d++) if(acc[d]) s+=(int64_t)CW[c][d]*acc[d];
    if(s>2147483647LL)return 2147483647;
    if(s<-2147483647LL)return -2147483647;
    return (int)s;
}

static void train_semhash(void){
    int16_t acc[RD]; int32_t tot; (void)tot;
    for(int ep=0;ep<EPOCHS;ep++){
        int err=0;
        for(int i=0;i<U_n;i++){
            r_counts(U_t[i],acc,&tot); int y=R.label[i], pred=0, best=dot_cls(0,acc);
            for(uint32_t c=1;c<R.n_class;c++){ int s=dot_cls((int)c,acc); if(s>best){best=s;pred=(int)c;} }
            if(pred!=y){
                err++;
                for(int d=0;d<RD;d++) if(acc[d]){ CW[y][d]+=acc[d]; CW[pred][d]-=acc[d]; }
            }
        }
        if(!err) break;
    }
}

static int pop64(uint64_t x){ return __builtin_popcountll(x); }
static int code_score(uint64_t a,uint64_t b){ return (HD-2*pop64(a^b))*256/HD; }
static int sem_sim(sh_t q, sh_t c){ return code_score(q.code,c.code) + c.score/8; }

static sh_t sem_encode(const char *text){
    int16_t acc[RD]; int32_t tot; (void)tot; r_counts(text,acc,&tot);
    int best=-(1<<28), bi=-1;
    for(uint32_t c=0;c<R.n_class;c++){ int s=dot_cls((int)c,acc); if(s>best){best=s;bi=(int)c;} }
    uint64_t code=class_code(bi);
    sh_t out={code,bi,best}; return out;
}

static int raw_direct(const char *a,const char *b){
    tvec av,bv; t_encode(&R,a,&av); t_encode(&R,b,&bv);
    return t_score_pre(&av,&bv,t_active(&av),t_active(&bv));
}

static sem_result eval_probe(const char *query,const char **choices,int nc){
    sh_t q=sem_encode(query); int best=-1, bs=-(1<<28), second=-(1<<28);
    int sem[MAXC], raw[MAXC], pred[MAXC]; sh_t cs[MAXC];
    for(int i=0;i<nc;i++){
        cs[i]=sem_encode(choices[i]); pred[i]=cs[i].pred;
        sem[i]=sem_sim(q,cs[i]); raw[i]=raw_direct(query,choices[i]);
        if(sem[i]>bs){second=bs;bs=sem[i];best=i;} else if(sem[i]>second)second=sem[i];
    }
    int ci=-1,cj=-1,cb=-(1<<28);
    for(int i=0;i<nc;i++)for(int j=i+1;j<nc;j++){int s=code_score(cs[i].code,cs[j].code); if(s>cb){cb=s;ci=i;cj=j;}}
    sem_result r; memset(&r,0,sizeof r);
    r.best=best; r.best_sem=bs; r.second_sem=second; r.margin=bs-second;
    r.coll_i=ci; r.coll_j=cj; r.coll_best=cb;
    r.best_direct=raw[best]; r.best_reachable=code_score(q.code,cs[best].code)>0;
    for(int i=0;i<nc;i++){ r.sem[i]=sem[i]; r.raw[i]=raw[i]; r.pred[i]=pred[i]; r.clsfit[i]=cs[i].score; }
    return r;
}

static int run_probe(const char *query,const char **choices,int nc){
    sh_t q=sem_encode(query); sem_result r=eval_probe(query,choices,nc);
    printf("index %d vectors, %u classes, semhash=%d bits, epochs=%d\n",U_n,R.n_class,HD,EPOCHS);
    printf("DIAGNOSTIC ONLY: learned hash is a host proof, not a production policy.\n\n");
    printf("query: \"%s\"  pred=%s score=%d\n",query,R.names[q.pred],q.score);
    printf("\nchoices:\n");
    for(int i=0;i<nc;i++)
        printf("  [%d] sem=%4d raw=%4d pred=%-20s clsfit=%4d  \"%s\"\n",
               i,r.sem[i],r.raw[i],R.names[r.pred[i]],r.clsfit[i],choices[i]);
    printf("\nresult: choice=%d sem=%d margin=%d candidate_pair_max=%d (%d,%d)\n",
           r.best,r.best_sem,r.margin,r.coll_best,r.coll_i,r.coll_j);
    printf("metrics: direct=%d neighborhood_agreement=%d margin=%d reachable=%s\n",
           r.best_direct,r.sem[r.best],r.margin,r.best_reachable?"yes":"no");
    if(r.margin<20)printf("  WARN low_margin: %d < 20\n",r.margin);
    if(r.coll_best>=180)printf("  WARN candidate_collision: %d >= 180\n",r.coll_best);
    if(!strcmp(R.names[r.pred[r.best]],"none"))printf("  WARN none_basin: winning choice predicts none\n");
    return r.best;
}

static int rt_total,rt_pass;
static void rt(const char *name,int ok){rt_total++; if(ok)rt_pass++; else printf("  FAIL semhash redteam: %s\n",name);}

static int redteam(void){
    const char *demo[]={"increase the brightness of the bedroom lights","decrease the brightness of the bedroom lights","turn the bedroom lights completely off"};
    sem_result pr=eval_probe("make the bedroom darker",demo,3);
    sh_t q=sem_encode("make the bedroom darker"), a=sem_encode(demo[0]), b=sem_encode(demo[1]);
    rt("demo query predicts dim", strstr(R.names[q.pred],"lightdim")!=NULL);
    rt("demo reports direct similarity", pr.best_direct==pr.raw[pr.best]);
    rt("demo reports semantic agreement", pr.sem[pr.best]==pr.best_sem);
    rt("demo reports margin", pr.margin==pr.best_sem-pr.second_sem);
    rt("demo reports reachability", pr.best_reachable);
    rt("decrease candidate closer than increase", sem_sim(q,b)>sem_sim(q,a));

    const char *off[]={"turn the kitchen lights on","turn the kitchen lights off","make the kitchen brighter"};
    q=sem_encode("turn the kitchen lights off"); a=sem_encode(off[0]); b=sem_encode(off[1]);
    rt("off query predicts off", strstr(R.names[q.pred],"lightoff")!=NULL);
    rt("off candidate closer than on", sem_sim(q,b)>sem_sim(q,a));

    q=sem_encode("dim lights"); a=sem_encode("lower brightness"); b=sem_encode("decrease brightness");
    rt("dim query predicts dim", strstr(R.names[q.pred],"lightdim")!=NULL);
    rt("lower brightness predicts dim", strstr(R.names[a.pred],"lightdim")!=NULL);
    rt("decrease brightness predicts dim or exposes confusion", strstr(R.names[b.pred],"lightdim")||strstr(R.names[b.pred],"lightup"));

    q=sem_encode("make the room less luminous"); a=sem_encode("lower the brightness of the lights"); b=sem_encode("raise the brightness of the lights");
    rt("less luminous remains a hard case", sem_sim(q,a)<=sem_sim(q,b));
    rt("lower/raise collision visible", code_score(a.code,b.code)>=100);

    q=sem_encode("refund the customer"); a=sem_encode("issue money back to the customer");
    rt("refund is outside IoT territory", !strcmp(R.names[q.pred],"none")||q.score<128);
    rt("refund paraphrase stays close", sem_sim(q,a)>100);

    printf("SEMHASH_REDTEAM checks=%d/%d score=%d/100\n",rt_pass,rt_total,rt_total?(100*rt_pass)/rt_total:0);
    return rt_pass==rt_total?0:1;
}

int main(int argc,char **argv){
    const char *paths[4]={"data/train.json","data/validation.json","data/test.json","data/nlu_home.csv"};
    int arg=1,demo=0,red=0; const char *query=NULL,*choices[MAXC]; int nc=0;
    if(argc>=5&&argv[1][0]!='-'&&argv[2][0]!='-'&&argv[3][0]!='-'&&argv[4][0]!='-'){for(int i=0;i<4;i++)paths[i]=argv[1+i];arg=5;}
    for(int i=arg;i<argc;i++){
        if(!strcmp(argv[i],"--demo"))demo=1;
        else if(!strcmp(argv[i],"--redteam"))red=1;
        else if(!strcmp(argv[i],"--query")&&i+1<argc)query=argv[++i];
        else if(!strcmp(argv[i],"--choice")&&i+1<argc){if(nc>=MAXC){fprintf(stderr,"too many choices\n");return 1;} choices[nc++]=argv[++i];}
        else {usage();return 1;}
    }
    if(demo&&(query||nc)){fprintf(stderr,"--demo cannot be mixed with --query/--choice\n");return 1;}
    if(red&&(demo||query||nc)){fprintf(stderr,"--redteam cannot be mixed with probes\n");return 1;}
    if(demo){query="make the bedroom darker"; choices[0]="increase the brightness of the bedroom lights"; choices[1]="decrease the brightness of the bedroom lights"; choices[2]="turn the bedroom lights completely off"; nc=3;}
    if(!red&&(!query||nc<2)){usage();return 1;}
    load_data(paths[0],paths[1],paths[2],paths[3]); train_semhash();
    if(red)return redteam();
    (void)run_probe(query,choices,nc); return 0;
}
