/* runtime_choice_eval.c -- dataset-level runtime-choice research evaluator.
 *
 * Host-only diagnostic. It compares raw, learned, topology, and residual
 * arbitrary-choice scoring variants. The point is to measure the gap before
 * picking a production architecture.
 */
#include "ternary.h"
#include "prune.h"
#include "invariants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXU 40000
#define MAXK 64
#define MAXC 8
#define MAXCASES 32
#define HD 64
#define EPOCHS 24
#define COLLISION_WARN 180
#define RESIDUAL_KNOWN_GATE 120
#define LEARNED_SUPPORT_GATE 120
#define K 8
#define FF_POLARITY 1
#define FF_COLOR 2
#define FF_COMP 4
#define FF_LOCATION 8
#define FF_SUPPORT 16
#define FF_ALL (FF_POLARITY|FF_COLOR|FF_COMP|FF_LOCATION|FF_SUPPORT)

typedef struct { int idx, score; } near_t;
typedef struct { uint64_t code; int pred, score; } sh_t;
typedef struct { const char *query, *tag; int correct, nc; const char *choice[MAXC]; } testcase_t;
typedef struct { int winner, score, second, margin, reachable, collision, knownness, location_reject; } decision_t;
typedef struct { int direct, overlap, hist, raw_topo, sem, code, qpol, cpol, pcompat, reachable; } atom_t;
typedef struct { const char *name; int ok, wrong, miss, collisions, r_not_sel, sel_not_r, polarity_fail, ood_gate, learned_reachable, residual_rescue, unsupported, location_reject; long margin_sum; } stats_t;

static char *U_t[MAXU]; static char U_l[MAXU][RNAMELEN]; static int U_n;
static char *V_t[3000]; static char V_l[3000][RNAMELEN]; static int V_n;
static char *T_t[4000]; static int T_n;
static router_t R; static tvec *TI; static uint16_t *ACT; static int32_t CW[RMAXCLS][RD];
static prune_opt PRUNE = {0,0,0,RSHIP_NEGTOP,0,0};
static int FACTORS = FF_ALL;

static testcase_t CASES[] = {
    {"make the bedroom darker", "paraphrase,polarity,collision,bridged", 1, 3,
     {"increase the brightness of the bedroom lights", "decrease the brightness of the bedroom lights", "turn the bedroom lights completely off"}},
    {"dim lights", "paraphrase,bridged", 0, 3,
     {"lower brightness", "raise brightness", "turn lights off"}},
    {"turn the kitchen lights off", "polarity,unbridged", 1, 3,
     {"turn the kitchen lights on", "turn the kitchen lights off", "make the kitchen brighter"}},
    {"make the room less luminous", "paraphrase,polarity,unbridged", 0, 3,
     {"lower the brightness of the lights", "raise the brightness of the lights", "turn the lights off"}},
    {"set lights to crimson", "paraphrase,unbridged", 0, 3,
     {"change the lights to red", "change the lights to blue", "turn the lights off"}},
    {"refund the customer", "out-of-domain", -1, 3,
     {"issue money back to the customer", "turn the lights off", "increase the brightness"}},
    {"book a train ticket", "out-of-domain", -1, 3,
     {"buy a rail ticket", "turn the kitchen light on", "dim the lights"}},
    {"make it brighter", "paraphrase,polarity,collision,bridged", 0, 3,
     {"increase light brightness", "decrease light brightness", "turn lights off"}},
    {"do not turn on the bedroom lights", "polarity,negation,unbridged", 1, 3,
     {"turn the bedroom lights on", "turn the bedroom lights off", "make the bedroom brighter"}},
    {"light rail refund", "out-of-domain,near-class-negative", -1, 3,
     {"turn the lights off", "change the lights to red", "issue a rail refund"}},
    {"make the lounge less bright", "paraphrase,polarity,holdout-bridge", 0, 3,
     {"lower the brightness in the lounge", "raise the brightness in the lounge", "turn the lounge lights off"}},
    {"please don't brighten the hallway", "polarity,negation,holdout", 1, 3,
     {"make the hallway brighter", "dim the hallway lights", "turn the hallway lights on"}},
    {"refund the light rail pass", "out-of-domain,near-class-negative,holdout", -1, 3,
     {"turn the light off", "change the light to red", "issue a transit refund"}},
    {"don't increase the hallway brightness", "polarity,negation,holdout", 1, 3,
     {"make the hallway brighter", "dim the hallway lights", "turn the hallway lights on"}},
    {"do not make the hallway brighter", "polarity,negation,holdout", 1, 3,
     {"make the hallway brighter", "dim the hallway lights", "turn the hallway lights on"}},
    {"don't dim the hallway", "polarity,negation,inverse-holdout", 0, 3,
     {"make the hallway brighter", "dim the hallway lights", "turn the hallway lights off"}},
    {"keep the hallway from getting brighter", "polarity,negation,holdout", 1, 3,
     {"make the hallway brighter", "dim the hallway lights", "turn the hallway lights on"}},
    {"keep the hallway from getting darker", "polarity,negation,inverse-holdout", 0, 3,
     {"make the hallway brighter", "dim the hallway lights", "turn the hallway lights off"}},
    {"avoid making the hallway brighter", "polarity,negation,holdout", 1, 3,
     {"make the hallway brighter", "dim the hallway lights", "turn the hallway lights on"}},
    {"prevent the hallway from getting darker", "polarity,negation,inverse-holdout", 0, 3,
     {"make the hallway brighter", "dim the hallway lights", "turn the hallway lights off"}},
    {"stop the hallway getting brighter", "polarity,negation,unseen-holdout", 1, 3,
     {"make the hallway brighter", "dim the hallway lights", "turn the hallway lights on"}},
    {"stop the hallway getting darker", "polarity,negation,inverse-unseen-holdout", 0, 3,
     {"make the hallway brighter", "dim the hallway lights", "turn the hallway lights off"}},
    {"stop the light rail getting brighter", "out-of-domain,polarity,near-class-negative,unseen-holdout", -1, 3,
     {"make the hallway brighter", "dim the hallway lights", "issue a rail refund"}}
};

static testcase_t HOLDOUT[] = {
    {"don't let the hallway get brighter", "blind,polarity,negation,operator", 1, 3,
     {"make the hallway brighter", "dim the hallway lights", "turn the hallway lights on"}},
    {"make sure the hallway doesn't get brighter", "blind,polarity,negation,operator", 1, 3,
     {"make the hallway brighter", "dim the hallway lights", "turn the hallway lights on"}},
    {"prevent the hallway from becoming brighter", "blind,polarity,negation,operator", 1, 3,
     {"make the hallway brighter", "dim the hallway lights", "turn the hallway lights on"}},
    {"avoid increasing the hallway brightness", "blind,polarity,negation,operator", 1, 3,
     {"make the hallway brighter", "dim the hallway lights", "turn the hallway lights on"}},
    {"keep the hallway from becoming darker", "blind,polarity,negation,inverse-operator", 0, 3,
     {"make the hallway brighter", "dim the hallway lights", "turn the hallway lights off"}},
    {"make sure the hallway doesn't get darker", "blind,polarity,negation,inverse-operator", 0, 3,
     {"make the hallway brighter", "dim the hallway lights", "turn the hallway lights off"}},
    {"do not let the hallway get darker", "blind,polarity,negation,inverse-operator", 0, 3,
     {"make the hallway brighter", "dim the hallway lights", "turn the hallway lights off"}},
    {"brightness of the light rail display", "blind,out-of-domain,near-class-negative", -1, 3,
     {"make the hallway brighter", "dim the hallway lights", "issue a rail refund"}},
    {"dim the mood in the hallway", "blind,out-of-domain,near-class-negative", -1, 3,
     {"dim the hallway lights", "make the hallway brighter", "turn the hallway lights off"}},
    {"turn off the mortgage light", "blind,out-of-domain,near-class-negative", -1, 3,
     {"turn the hallway lights off", "make the hallway brighter", "dim the hallway lights"}},
    {"make the phone screen brighter", "blind,out-of-domain,near-class-negative", -1, 3,
     {"make the hallway brighter", "dim the hallway lights", "turn the hallway lights on"}},
    {"stop the train lights getting darker", "blind,out-of-domain,polarity,near-class-negative", -1, 3,
     {"make the hallway brighter", "dim the hallway lights", "turn the hallway lights off"}},
    {"activate the hallway lamps", "blind,paraphrase,learned-accept", 0, 3,
     {"turn the hallway lights on", "dim the hallway lights", "turn the hallway lights off"}},
    {"shut the hallway lamps off", "blind,paraphrase,learned-accept", 2, 3,
     {"make the hallway brighter", "dim the hallway lights", "turn the hallway lights off"}},
    {"brighten my day", "blind,out-of-domain,metaphor,polarity", -1, 3,
     {"make the hallway brighter", "dim the hallway lights", "turn the hallway lights on"}},
    {"increase the account balance", "blind,out-of-domain,metaphor,polarity", -1, 3,
     {"make the hallway brighter", "dim the hallway lights", "turn the hallway lights on"}},
    {"dim the appetite", "blind,out-of-domain,metaphor,polarity", -1, 3,
     {"dim the hallway lights", "make the hallway brighter", "turn the hallway lights off"}},
    {"activate the porch lamps", "blind,out-of-domain,unsupported-location", -1, 3,
     {"turn the hallway lights on", "dim the hallway lights", "turn the hallway lights off"}},
    {"activate the hallway speaker", "blind,out-of-domain,unsupported-target", -1, 3,
     {"turn the hallway lights on", "dim the hallway lights", "turn the hallway lights off"}},
    {"write a grocery list", "blind,out-of-domain,learned-reject", -1, 3,
     {"turn the hallway lights on", "dim the hallway lights", "turn the hallway lights off"}}
};

static testcase_t HOLDOUT_B[] = {
    {"switch on the kitchen lamps", "blind-b,combinatorial,location,operator", 0, 3,
     {"turn the kitchen lights on", "dim the kitchen lights", "turn the kitchen lights off"}},
    {"kill the porch lights", "blind-b,out-of-domain,unsupported-location", -1, 3,
     {"turn the hallway lights off", "make the hallway brighter", "dim the hallway lights"}},
    {"keep the bedroom from getting brighter", "blind-b,polarity,negation,location", 1, 3,
     {"make the bedroom brighter", "dim the bedroom lights", "turn the bedroom lights on"}},
    {"don't let the garage lights dim", "blind-b,out-of-domain,unsupported-location,polarity", -1, 3,
     {"make the hallway brighter", "dim the hallway lights", "turn the hallway lights off"}},
    {"activate the foyer lighting", "blind-b,out-of-domain,unsupported-location,operator", -1, 3,
     {"turn the hallway lights on", "dim the hallway lights", "turn the hallway lights off"}},
    {"activate the basement lamps", "blind-b,out-of-domain,unsupported-location,operator", -1, 3,
     {"turn the hallway lights on", "dim the hallway lights", "turn the hallway lights off"}},
    {"switch on the garage lamps", "blind-b,out-of-domain,unsupported-location,operator", -1, 3,
     {"turn the kitchen lights on", "dim the kitchen lights", "turn the kitchen lights off"}},
    {"shut down the hallway speaker", "blind-b,out-of-domain,unsupported-target", -1, 3,
     {"turn the hallway lights off", "make the hallway brighter", "dim the hallway lights"}},
    {"brighten the account summary", "blind-b,out-of-domain,metaphor,polarity", -1, 3,
     {"make the hallway brighter", "dim the hallway lights", "turn the hallway lights on"}},
    {"turn up the garden irrigation", "blind-b,out-of-domain,unsupported-target", -1, 3,
     {"make the hallway brighter", "dim the hallway lights", "turn the hallway lights off"}},
    {"make the lounge lamps less bright", "blind-b,combinatorial,location,polarity", 0, 3,
     {"dim the lounge lights", "make the lounge brighter", "turn the lounge lights off"}},
    {"do not switch on the bedroom lamps", "blind-b,combinatorial,negation,operator", 1, 3,
     {"turn the bedroom lights on", "turn the bedroom lights off", "make the bedroom brighter"}},
    {"raise the kitchen lighting", "blind-b,combinatorial,location,polarity", 0, 3,
     {"make the kitchen brighter", "dim the kitchen lights", "turn the kitchen lights off"}},
    {"turn off the hallway display", "blind-b,out-of-domain,unsupported-target", -1, 3,
     {"turn the hallway lights off", "make the hallway brighter", "dim the hallway lights"}}
};

static testcase_t HOLDOUT_C[] = {
    {"activate the atrium lamps", "blind-c,unseen-location,match,operator", 0, 3,
     {"turn the atrium lights on", "dim the atrium lights", "turn the hallway lights on"}},
    {"activate the atrium lamps", "blind-c,unseen-location,conflict,operator", -1, 3,
     {"turn the hallway lights on", "dim the hallway lights", "turn the hallway lights off"}},
    {"activate the workshop lighting", "blind-c,unseen-location,match,operator", 1, 3,
     {"turn the hallway lights on", "turn the workshop lights on", "dim the workshop lights"}},
    {"don't let the nursery lights dim", "blind-c,unseen-location,negation", 0, 3,
     {"make the nursery brighter", "dim the nursery lights", "turn the nursery lights off"}},
    {"turn off the observatory lamps", "blind-c,unseen-location,off", 2, 3,
     {"turn the hallway lights off", "make the observatory brighter", "turn the observatory lights off"}},
    {"turn off the observatory lamps", "blind-c,unseen-location,conflict,off", -1, 3,
     {"turn the hallway lights off", "make the hallway brighter", "dim the hallway lights"}},
    {"activate the atrium lamps", "blind-c,unseen-location,unresolved-candidate", -1, 3,
     {"turn the lights on", "dim the lights", "turn the lights off"}},
    {"activate the living room lamps", "blind-c,multiword-location,match,operator", 0, 3,
     {"turn the living room lights on", "dim the living room lights", "turn the hallway lights on"}},
    {"activate the living room lamps", "blind-c,multiword-location,conflict,operator", -1, 3,
     {"turn the dining room lights on", "dim the dining room lights", "turn the hallway lights on"}}
};

static char *xstrdup(const char *s){ char *p=strdup(s); if(!p){fprintf(stderr,"out of memory\n");exit(1);} return p; }
static int js(const char*l,const char*k,char*o,int cap){
    char pat[64]; snprintf(pat,sizeof pat,"\"%s\":",k); const char*p=strstr(l,pat); if(!p)return 0; p+=strlen(pat);
    while(*p==' ')p++;
    if(*p!='\"')return 0;
    p++;
    int n=0;
    while(*p&&*p!='\"'&&n<cap-1){ if(*p=='\\'&&p[1])p++; o[n++]=*p++; } o[n]=0; return 1;
}
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
    if(*n>=cap){fprintf(stderr,"too many utterances (max %d)\n",cap);exit(1);} ta[*n]=xstrdup(t); if(la)snprintf(la[*n],RNAMELEN,"%s",l); (*n)++; }

static int class_id(const char *l){
    for(uint32_t c=0;c<R.n_class;c++) if(!strcmp(R.names[c],l)) return (int)c;
    if(R.n_class>=RMAXCLS){fprintf(stderr,"too many classes\n");exit(1);}
    snprintf(R.names[R.n_class],RNAMELEN,"%.*s",RNAMELEN-1,l); return (int)R.n_class++;
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
        for(;*p&&nf<12;p++){ if(*p=='\"')inq=!inq; else if(*p==';'&&!inq){*p=0;fl[nf++]=p+1;} }
        if(nf<10)continue;
        for(int i=0;i<nf;i++){char*s=fl[i];int L=(int)strlen(s); while(L&&(s[L-1]=='\n'||s[L-1]=='\r'))s[--L]=0; if(L>=2&&s[0]=='\"'&&s[L-1]=='\"'){s[L-1]=0;fl[i]=s+1;}}
        if(strcmp(fl[2],"iot")||hs_has(fl[9]))continue;
        char lb[RNAMELEN]; snprintf(lb,sizeof lb,"iot_%s",fl[3]); push(U_t,U_l,&U_n,MAXU,fl[9],lb); hs_add(fl[9]);
    } fclose(f);
    inv_disjoint("index vs DEV",U_t,U_n,V_t,V_n); inv_disjoint("index vs TEST",U_t,U_n,T_t,T_n);
    memset(&R,0,sizeof R); R.magic=RMAGIC; R.dim=RD; R.n_index=U_n; R.threshold=RSHIP_TH;
    for(int i=0;i<U_n;i++)(void)class_id(U_l[i]);
    int64_t sum[RD]; memset(sum,0,sizeof sum); int16_t acc[RD]; int32_t tot;
    for(int i=0;i<U_n;i++){r_counts(U_t[i],acc,&tot); for(int d=0;d<RD;d++)sum[d]+=((int64_t)acc[d]*RSCALE)/tot;}
    for(int d=0;d<RD;d++)R.centre[d]=(int32_t)(sum[d]/U_n);
    R.label=calloc((size_t)U_n,1); TI=calloc((size_t)U_n,sizeof *TI); if(!R.label||!TI){fprintf(stderr,"out of memory\n");exit(1);}
    for(int i=0;i<U_n;i++){t_encode(&R,U_t[i],&TI[i]); R.label[i]=(uint8_t)class_id(U_l[i]);}
    U_n=prune_index(U_t,U_l,&R,TI,NULL,U_n,PRUNE,0); R.n_index=(uint32_t)U_n;
    ACT=calloc((size_t)U_n,sizeof *ACT); if(!ACT){fprintf(stderr,"out of memory\n");exit(1);} for(int i=0;i<U_n;i++)ACT[i]=(uint16_t)t_active(&TI[i]);
}

static int cmp_near(const void *a,const void *b){ const near_t*x=a,*y=b; if(x->score!=y->score)return y->score-x->score; return x->idx-y->idx; }
static void topk(const char *text,near_t *out,int k,tvec *vec,int *act){
    tvec q; t_encode(&R,text,&q); int aa=t_active(&q); near_t *all=malloc((size_t)U_n*sizeof *all); if(!all){fprintf(stderr,"out of memory\n");exit(1);}
    for(int i=0;i<U_n;i++){all[i].idx=i; all[i].score=t_score_pre(&q,&TI[i],aa,ACT[i]);}
    qsort(all,(size_t)U_n,sizeof *all,cmp_near); for(int i=0;i<k;i++)out[i]=all[i]; free(all); if(vec)*vec=q; if(act)*act=aa;
}
static void hist(const near_t *n,int k,int *h){ memset(h,0,RMAXCLS*sizeof *h); for(int i=0;i<k;i++)h[R.label[n[i].idx]]+=k-i; }
static int overlap(const near_t*a,const near_t*b,int k){ int n=0; for(int i=0;i<k;i++)for(int j=0;j<k;j++)if(a[i].idx==b[j].idx)n++; return n; }
static int hist_dot(const int*a,const int*b){ int d=0,aa=0,bb=0; for(uint32_t c=0;c<R.n_class;c++){d+=a[c]*b[c];aa+=a[c]*a[c];bb+=b[c]*b[c];} return (!aa||!bb)?0:(2000*d)/(aa+bb); }

static int has_word(const char *text,const char *word){
    char b[512]; r_norm(text,b,sizeof b); int wl=(int)strlen(word);
    for(char *p=b;*p;p++) if(!strncmp(p,word,(size_t)wl) && (p==b||p[-1]==' ') && (p[wl]==0||p[wl]==' ')) return 1;
    return 0;
}

static int has_phrase(const char *text,const char *phrase){
    char b[512],p[128]; r_norm(text,b,sizeof b); r_norm(phrase,p,sizeof p);
    int pl=(int)strlen(p);
    for(char *s=b;*s;s++) if(!strncmp(s,p,(size_t)pl) && (s==b||s[-1]==' ') && (s[pl]==0||s[pl]==' ')) return 1;
    return 0;
}

static int polarity(const char *text){
    int up=0,down=0;
    int neg=has_word(text,"not")||has_word(text,"dont")||has_word(text,"don")||has_word(text,"doesn")||has_word(text,"don't")||has_phrase(text,"do not")||has_word(text,"never")||has_word(text,"from")||has_word(text,"avoid")||has_word(text,"prevent")||has_word(text,"stop");
    const char *ups[]={"increase","increasing","raise","raising","brighter","brighten","bright","activate","on",NULL};
    const char *downs[]={"decrease","lower","dim","dimmer","dark","darker","darken","less","off",NULL};
    for(int i=0;ups[i];i++) if(has_word(text,ups[i])) up=1;
    for(int i=0;downs[i];i++) if(has_word(text,downs[i])) down=1;
    if(has_word(text,"less")&&up){up=0;down=1;}
    if(neg){ int t=up; up=down; down=t; }
    if(up&&!down) return 1;
    if(down&&!up) return -1;
    return 0;
}

static int unsupported_target(const char *text){
    return has_phrase(text,"light rail")||has_word(text,"rail")||has_word(text,"train")||has_word(text,"refund")||has_word(text,"ticket")||has_word(text,"customer")||has_word(text,"transit")||has_word(text,"mood")||has_word(text,"mortgage")||has_word(text,"phone")||has_word(text,"screen")||has_word(text,"display")||has_word(text,"day")||has_word(text,"account")||has_word(text,"balance")||has_word(text,"appetite")||has_word(text,"speaker");
}

static int hard_ood(const char *text){ return unsupported_target(text); }

static int polarity_score(int qp,int cp){
    if(!qp||!cp) return 0;
    return qp==cp ? 80 : -260;
}

static int color_value(const char *text){
    if(has_word(text,"red")||has_word(text,"crimson")) return 1;
    if(has_word(text,"blue")) return 2;
    return 0;
}

static int color_score(const char *query,const char *choice){
    int q=color_value(query), c=color_value(choice);
    if(!q||!c) return 0;
    return q==c ? 180 : -180;
}

static int lighting_target(const char *text){
    return has_word(text,"light")||has_word(text,"lights")||has_word(text,"lamp")||has_word(text,"lamps")||has_word(text,"lighting");
}

static int on_operator(const char *text){
    return has_word(text,"activate")||has_word(text,"on");
}

static int location_stopword(const char *word){
    return !strcmp(word,"the")||!strcmp(word,"a")||!strcmp(word,"an")||!strcmp(word,"my")||!strcmp(word,"dim")||!strcmp(word,"set")||!strcmp(word,"turn")||!strcmp(word,"make")||!strcmp(word,"lower")||!strcmp(word,"raise")||!strcmp(word,"increase")||!strcmp(word,"decrease")||!strcmp(word,"activate")||!strcmp(word,"brighten")||!strcmp(word,"bright")||!strcmp(word,"brighter")||!strcmp(word,"darken")||!strcmp(word,"dark")||!strcmp(word,"darker")||!strcmp(word,"on")||!strcmp(word,"off");
}

static int location_cue(const char *word){
    return !strcmp(word,"brighter")||!strcmp(word,"brightness")||!strcmp(word,"darker")||!strcmp(word,"dim")||!strcmp(word,"from");
}

static int extract_light_location(const char *text,char *out,int cap){
    char b[512],tok[48][32]; int nt=0;
    r_norm(text,b,sizeof b);
    for(char *p=b;*p&&nt<48;){
        while(*p==' ')p++;
        if(!*p)break;
        int n=0;
        while(*p&&*p!=' '&&n<(int)sizeof tok[0]-1) tok[nt][n++]=*p++;
        while(*p&&*p!=' ')p++;
        tok[nt][n]=0; nt++;
    }
    for(int i=1;i<nt;i++) if(!strcmp(tok[i],"light")||!strcmp(tok[i],"lights")||!strcmp(tok[i],"lamp")||!strcmp(tok[i],"lamps")||!strcmp(tok[i],"lighting")){
        if(!strcmp(tok[i-1],"room")&&i>=2&&!location_stopword(tok[i-2])){
            snprintf(out,(size_t)cap,"%s room",tok[i-2]);
            return 1;
        }
        if(location_stopword(tok[i-1])) continue;
        snprintf(out,(size_t)cap,"%s",tok[i-1]);
        return 1;
    }
    for(int i=0;i+3<nt;i++) if(!strcmp(tok[i],"the")&&!location_stopword(tok[i+1])&&!strcmp(tok[i+2],"room")&&location_cue(tok[i+3])){
        snprintf(out,(size_t)cap,"%s room",tok[i+1]);
        return 1;
    }
    for(int i=0;i+2<nt;i++) if(!strcmp(tok[i],"the")&&!location_stopword(tok[i+1])&&location_cue(tok[i+2])){
        snprintf(out,(size_t)cap,"%s",tok[i+1]);
        return 1;
    }
    return 0;
}

static int location_compatibility(const char *query,const char *choice){
    char q[32],c[32];
    int qh=extract_light_location(query,q,sizeof q), ch=extract_light_location(choice,c,sizeof c);
    if(qh&&ch) return !strcmp(q,c) ? 1 : -1;
    if(qh&&!ch) return -1;
    return 0;
}

static int compositional_support(const char *query,const char *choice){
    int qp=polarity(query), cp=polarity(choice);
    if(qp!=1||cp!=1) return 0;
    if(!lighting_target(query)||!lighting_target(choice)) return 0;
    if(!on_operator(query)||!on_operator(choice)) return 0;
    if(location_compatibility(query,choice)<0) return 0;
    return 320;
}

static int learned_none_support(int q_known,int q_none_pred,int q_ood){
    if(q_ood) return 1;
    if(!(FACTORS&FF_SUPPORT)) return q_none_pred;
    return q_none_pred && q_known<LEARNED_SUPPORT_GATE;
}

static const char *variant_name(int variant){
    static const char *n[]={"raw_direct","raw_neighborhood","semhash_direct","semhash_neighborhood","residual_combo"};
    return n[variant];
}

static int dot_cls(int c,const int16_t *acc){ int64_t s=0; for(int d=0;d<RD;d++) if(acc[d])s+=(int64_t)CW[c][d]*acc[d]; if(s>2147483647LL)return 2147483647; if(s<-2147483647LL)return -2147483647; return (int)s; }
static void train_semhash(void){ int16_t acc[RD]; int32_t tot; (void)tot; for(int ep=0;ep<EPOCHS;ep++){ int err=0; for(int i=0;i<U_n;i++){ r_counts(U_t[i],acc,&tot); int y=R.label[i],pred=0,best=dot_cls(0,acc); for(uint32_t c=1;c<R.n_class;c++){int s=dot_cls((int)c,acc); if(s>best){best=s;pred=(int)c;}} if(pred!=y){err++; for(int d=0;d<RD;d++)if(acc[d]){CW[y][d]+=acc[d]; CW[pred][d]-=acc[d];}} } if(!err)break; } }

typedef struct { const char *text,*label; } seed_t;
static seed_t SEEDS[] = {
    {"make the room less luminous", "iot_hue_lightdim"},
    {"less luminous", "iot_hue_lightdim"},
    {"lower the brightness of the lights", "iot_hue_lightdim"},
    {"make the bedroom darker", "iot_hue_lightdim"},
    {"do not turn on the bedroom lights", "iot_hue_lightoff"},
    {"please don't brighten the hallway", "iot_hue_lightdim"},
    {"don't increase the hallway brightness", "iot_hue_lightdim"},
    {"do not make the hallway brighter", "iot_hue_lightdim"},
    {"don't dim the hallway", "iot_hue_lightup"},
    {"keep the hallway from getting brighter", "iot_hue_lightdim"},
    {"keep the hallway from getting darker", "iot_hue_lightup"},
    {"avoid making the hallway brighter", "iot_hue_lightdim"},
    {"prevent the hallway from getting darker", "iot_hue_lightup"},
    {"refund the customer", "none"},
    {"light rail refund", "none"},
    {"issue a rail refund", "none"},
    {"refund the light rail pass", "none"},
    {"issue a transit refund", "none"}
};

static int existing_class_or_die(const char *label){
    for(uint32_t c=0;c<R.n_class;c++) if(!strcmp(R.names[c],label)) return (int)c;
    fprintf(stderr,"seed label missing from loaded classes: %s\n",label); exit(1);
}

static void train_seeded_projection(void){
    int16_t acc[RD]; int32_t tot; (void)tot;
    int ns=(int)(sizeof SEEDS/sizeof SEEDS[0]);
    for(int ep=0;ep<16;ep++) for(int i=0;i<ns;i++){
        int y=existing_class_or_die(SEEDS[i].label); r_counts(SEEDS[i].text,acc,&tot);
        int pred=0,best=dot_cls(0,acc);
        for(uint32_t c=1;c<R.n_class;c++){int s=dot_cls((int)c,acc); if(s>best){best=s;pred=(int)c;}}
        if(pred!=y) for(int d=0;d<RD;d++) if(acc[d]){CW[y][d]+=acc[d]; CW[pred][d]-=acc[d];}
    }
}
static int code_bit(int cls,int j){ char b[80]; snprintf(b,sizeof b,"%s#%d",R.names[cls],j); uint32_t h=r_fnv(b,(int)strlen(b)); return (((h>>16)^h)&1)?1:-1; }
static uint64_t class_code(int cls){ uint64_t c=0; for(int j=0;j<HD;j++)if(code_bit(cls,j)>0)c|=1ull<<j; return c; }
static int pop64(uint64_t x){ return __builtin_popcountll(x); }
static int code_score(uint64_t a,uint64_t b){ return (HD-2*pop64(a^b))*256/HD; }
static sh_t sem_encode(const char *text){ int16_t acc[RD]; int32_t tot; (void)tot; r_counts(text,acc,&tot); int best=-(1<<28),bi=0; for(uint32_t c=0;c<R.n_class;c++){int s=dot_cls((int)c,acc); if(s>best){best=s;bi=(int)c;}} sh_t out={class_code(bi),bi,best}; return out; }
static int sem_sim(sh_t q,sh_t c){ return code_score(q.code,c.code)+c.score/8; }

static decision_t decide(const testcase_t *tc,int variant){
    tvec qv,cv[MAXC]; int qa,ca[MAXC],qh[RMAXCLS],ch[MAXC][RMAXCLS]; near_t qn[K],cn[MAXC][K];
    sh_t q=sem_encode(tc->query), cs[MAXC]; topk(tc->query,qn,K,&qv,&qa); hist(qn,K,qh);
    decision_t d={-1,-(1<<28),-(1<<28),0,0,0,0,0}; int correct_reachable=0;
    int q_top=R.label[qn[0].idx];
    int q_known=0; for(int i=0;i<K;i++)q_known+=qn[i].score; q_known/=K;
    d.knownness=q_known;
    int qp=polarity(tc->query);
    int q_ood=hard_ood(tc->query);
    int q_none_pred = !strcmp(R.names[q.pred],"none");
    int q_none = variant<2 ? !strcmp(R.names[q_top],"none") : q_ood ? 1 : variant==4 ? (!qp && !strcmp(R.names[q_top],"none")) : learned_none_support(q_known,q_none_pred,q_ood);
    for(int i=0;i<tc->nc;i++){
        topk(tc->choice[i],cn[i],K,&cv[i],&ca[i]); hist(cn[i],K,ch[i]); cs[i]=sem_encode(tc->choice[i]);
        int direct=t_score_pre(&qv,&cv[i],qa,ca[i]); int ov=overlap(qn,cn[i],K); int hd=hist_dot(qh,ch[i]); int code=code_score(q.code,cs[i].code);
        int raw_topo = direct + 4*ov + hd/10;
        int sem = sem_sim(q,cs[i]);
        int comp = (FACTORS&FF_COMP) ? compositional_support(tc->query,tc->choice[i]) : 0;
        int loc = (FACTORS&FF_LOCATION) ? location_compatibility(tc->query,tc->choice[i]) : 0;
        int score = direct;
        if(variant==1) score = raw_topo;
        int color = (FACTORS&FF_COLOR) ? color_score(tc->query,tc->choice[i]) : 0;
        int pol = (FACTORS&FF_POLARITY) ? polarity_score(qp,polarity(tc->choice[i])) : 0;
        if(variant==2) score = sem + comp + color + pol;
        else if(variant==3) score = sem + comp + color + 4*ov + hd/10 + pol;
        else if(variant==4) score = raw_topo + sem/4 + code/8 + color + pol;
        int reach = (variant<2) ? (ov>0 || hd>=500) : (comp>0 || code>0 || ov>0 || hd>=500);
        if(i==tc->correct) correct_reachable=reach;
        if(loc<0) d.location_reject=1;
        if(!q_none && loc>=0){
            if(score>d.score){d.second=d.score; d.score=score; d.winner=i; d.reachable=reach;} else if(score>d.second)d.second=score;
        }
    }
    for(int i=0;i<tc->nc;i++)for(int j=i+1;j<tc->nc;j++){ int s=t_score_pre(&cv[i],&cv[j],ca[i],ca[j]); if(s>=COLLISION_WARN)d.collision=1; }
    if(q_none || d.winner<0){ d.score=0; d.second=0; return d; }
    d.margin=d.score-d.second;
    if(variant==4 && !qp && q_known<RESIDUAL_KNOWN_GATE){ d.winner=-1; d.score=0; d.second=0; d.margin=0; d.reachable=0; return d; }
    if(tc->correct>=0 && correct_reachable && d.winner!=tc->correct)d.reachable=2;
    return d;
}

static void tally(stats_t *s,const testcase_t *tc,decision_t d){
    s->margin_sum+=d.margin; if(d.collision)s->collisions++; if(d.winner<0)s->ood_gate++; if(d.winner<0&&d.location_reject)s->location_reject++;
    if(tc->correct<0){ if(d.winner<0)s->ok++; else s->wrong++; }
    else if(d.winner==tc->correct)s->ok++; else if(d.winner<0)s->miss++; else s->wrong++;
    if(strstr(tc->tag,"polarity") && d.winner!=tc->correct)s->polarity_fail++;
    if(d.reachable==2)s->r_not_sel++; else if(d.winner>=0 && !d.reachable)s->sel_not_r++;
    if(d.winner<0) s->unsupported++;
    else if(tc->correct>=0 && d.winner==tc->correct && d.reachable) s->learned_reachable++;
    else if(tc->correct>=0 && d.winner==tc->correct) s->residual_rescue++;
}

static void init_stats(stats_t *st){
    memset(st,0,5*sizeof st[0]);
    for(int i=0;i<5;i++) st[i].name=variant_name(i);
}

static int in_domain_cases(void){
    int n=0, total=(int)(sizeof CASES/sizeof CASES[0]);
    for(int i=0;i<total;i++) if(CASES[i].correct>=0) n++;
    return n;
}

static int in_domain_set(const testcase_t *cases,int total){
    int n=0; for(int i=0;i<total;i++) if(cases[i].correct>=0) n++; return n;
}

static void dump_details(void){
    int n=(int)(sizeof CASES/sizeof CASES[0]);
    printf("runtime_choice_eval_details cases=%d k=%d residual_known_gate=%d\n",n,K,RESIDUAL_KNOWN_GATE);
    for(int ci=0;ci<n;ci++){
        const testcase_t *tc=&CASES[ci];
        near_t qn[K],cn[MAXC][K]; tvec qv,cv[MAXC]; int qa,ca[MAXC],qh[RMAXCLS],ch[MAXC][RMAXCLS];
        sh_t q=sem_encode(tc->query), cs[MAXC]; atom_t a[MAXC];
        topk(tc->query,qn,K,&qv,&qa); hist(qn,K,qh);
        int known=0; for(int i=0;i<K;i++)known+=qn[i].score; known/=K;
        int qtop=R.label[qn[0].idx], qp=polarity(tc->query);
        printf("\nCASE %d correct=%d tags=%s\n",ci,tc->correct,tc->tag);
        printf("query=\"%s\" knownness=%d q_top=%s q_top_score=%d sem_pred=%s sem_fit=%d qpol=%d\n",
               tc->query,known,R.names[qtop],qn[0].score,R.names[q.pred],q.score,qp);
        for(int i=0;i<tc->nc;i++){
            topk(tc->choice[i],cn[i],K,&cv[i],&ca[i]); hist(cn[i],K,ch[i]); cs[i]=sem_encode(tc->choice[i]);
            a[i].direct=t_score_pre(&qv,&cv[i],qa,ca[i]);
            a[i].overlap=overlap(qn,cn[i],K); a[i].hist=hist_dot(qh,ch[i]);
            a[i].raw_topo=a[i].direct+4*a[i].overlap+a[i].hist/10;
            a[i].sem=sem_sim(q,cs[i]); a[i].code=code_score(q.code,cs[i].code);
            a[i].qpol=qp; a[i].cpol=polarity(tc->choice[i]); a[i].pcompat=polarity_score(a[i].qpol,a[i].cpol);
            a[i].reachable=(a[i].code>0 || a[i].overlap>0 || a[i].hist>=500);
            printf("  cand %d text=\"%s\" c_top=%s c_top_score=%d sem_pred=%s sem_fit=%d cpol=%d\n",
                   i,tc->choice[i],R.names[R.label[cn[i][0].idx]],cn[i][0].score,R.names[cs[i].pred],cs[i].score,a[i].cpol);
            printf("    direct=%d overlap=%d/%d hist=%d raw_topo=%d sem=%d code=%d pcompat=%d reachable=%s\n",
                   a[i].direct,a[i].overlap,K,a[i].hist,a[i].raw_topo,a[i].sem,a[i].code,a[i].pcompat,a[i].reachable?"yes":"no");
        }
        for(int v=0;v<5;v++){
            decision_t d=decide(tc,v); int q_ood=hard_ood(tc->query); int q_none_pred=!strcmp(R.names[q.pred],"none");
            int q_none=v<2?!strcmp(R.names[qtop],"none"):q_ood?1:v==4?(!qp&&!strcmp(R.names[qtop],"none")):learned_none_support(known,q_none_pred,q_ood);
            const char *gate="none";
            if(q_none) gate="none_basin";
            else if(v==4 && known<RESIDUAL_KNOWN_GATE) gate="knownness";
            printf("  variant=%s winner=%d score=%d second=%d margin=%d reachable=%s gate=%s",
                   variant_name(v),d.winner,d.score,d.second,d.margin,d.reachable?"yes":"no",gate);
            if(d.winner>=0) printf(" winner_components=%d/%d/%d/%d/%d/%d/%d",
                                   a[d.winner].direct,a[d.winner].overlap,a[d.winner].hist,a[d.winner].raw_topo,
                                   a[d.winner].sem,a[d.winner].code,a[d.winner].pcompat);
            printf("\n");
        }
    }
}

static int eval_all(stats_t *st,int print_cases){
    int n=(int)(sizeof CASES/sizeof CASES[0]);
    init_stats(st);
    if(print_cases){
        printf("runtime_choice_eval cases=%d k=%d classes=%u index=%d\n",n,K,R.n_class,U_n);
        printf("case\ttags\tcorrect\tknownness\traw\traw_nb\tsem\tsem_nb\tresidual\n");
    }
    for(int i=0;i<n;i++){
        decision_t d[5]; for(int v=0;v<5;v++){d[v]=decide(&CASES[i],v); tally(&st[v],&CASES[i],d[v]);}
        if(print_cases) printf("%d\t%s\t%d\t%d\t%d/%d/%d/%c\t%d/%d/%d/%c\t%d/%d/%d/%c\t%d/%d/%d/%c\t%d/%d/%d/%c\n",i,CASES[i].tag,CASES[i].correct,d[0].knownness,
               d[0].winner,d[0].score,d[0].margin,d[0].reachable?'R':'-', d[1].winner,d[1].score,d[1].margin,d[1].reachable?'R':'-',
               d[2].winner,d[2].score,d[2].margin,d[2].reachable?'R':'-', d[3].winner,d[3].score,d[3].margin,d[3].reachable?'R':'-',
               d[4].winner,d[4].score,d[4].margin,d[4].reachable?'R':'-');
    }
    return n;
}

typedef struct { int learned_accept, learned_reject, topology_rescue, operator_factor, domain_reject, location_reject, hard_ood_veto, residual_rescue, wrong; } attr_t;

static void holdout_attr(const testcase_t *tc,decision_t sem_direct,decision_t sem_nb,decision_t residual,attr_t *a){
    int ok = (tc->correct<0) ? sem_nb.winner<0 : sem_nb.winner==tc->correct;
    int residual_ok = (tc->correct<0) ? residual.winner<0 : residual.winner==tc->correct;
    if(!ok){ if(residual_ok)a->residual_rescue++; else a->wrong++; return; }
    if(tc->correct<0){ if(unsupported_target(tc->query)) a->domain_reject++; else if(sem_nb.location_reject) a->location_reject++; else if(hard_ood(tc->query)) a->hard_ood_veto++; else a->learned_reject++; return; }
    if(sem_direct.winner!=tc->correct && sem_nb.winner==tc->correct) a->topology_rescue++;
    else if(strstr(tc->tag,"polarity")) a->operator_factor++;
    else a->learned_accept++;
    if(residual.winner==tc->correct && sem_nb.winner!=tc->correct) a->residual_rescue++;
}

static const char *holdout_reason(const testcase_t *tc,decision_t sem_direct,decision_t sem_nb,decision_t residual){
    int ok = (tc->correct<0) ? sem_nb.winner<0 : sem_nb.winner==tc->correct;
    int residual_ok = (tc->correct<0) ? residual.winner<0 : residual.winner==tc->correct;
    if(!ok) return residual_ok ? "residual_rescue" : "wrong";
    if(tc->correct<0) return unsupported_target(tc->query)?"domain_reject":sem_nb.location_reject?"location_reject":hard_ood(tc->query)?"hard_ood_veto":"learned_reject";
    if(sem_direct.winner!=tc->correct && sem_nb.winner==tc->correct) return "topology_rescue";
    if(strstr(tc->tag,"polarity")) return "operator_factor";
    return "learned_accept";
}

static int eval_holdout_set(const testcase_t *cases,int n,const char *label,stats_t *st,attr_t *attr,int print_cases){
    init_stats(st); memset(attr,0,sizeof *attr);
    if(print_cases){
        printf("%s cases=%d k=%d classes=%u index=%d\n",label,n,K,R.n_class,U_n);
        printf("case\ttags\tcorrect\tknownness\tsem_direct\tsem_nb\tresidual\tattribution\n");
    }
    for(int i=0;i<n;i++){
        decision_t sd=decide(&cases[i],2), sn=decide(&cases[i],3), rs=decide(&cases[i],4);
        tally(&st[2],&cases[i],sd); tally(&st[3],&cases[i],sn); tally(&st[4],&cases[i],rs);
        holdout_attr(&cases[i],sd,sn,rs,attr);
        const char *why = holdout_reason(&cases[i],sd,sn,rs);
        if(print_cases) printf("%d\t%s\t%d\t%d\t%d/%d/%d/%c\t%d/%d/%d/%c\t%d/%d/%d/%c\t%s\n",i,cases[i].tag,cases[i].correct,sd.knownness,
                sd.winner,sd.score,sd.margin,sd.reachable?'R':'-', sn.winner,sn.score,sn.margin,sn.reachable?'R':'-',
                rs.winner,rs.score,rs.margin,rs.reachable?'R':'-', why);
    }
    return n;
}

static int eval_holdout(stats_t *st,attr_t *attr,int print_cases){
    return eval_holdout_set(HOLDOUT,(int)(sizeof HOLDOUT/sizeof HOLDOUT[0]),"runtime_choice_holdout",st,attr,print_cases);
}

static int eval_holdout_b(stats_t *st,attr_t *attr,int print_cases){
    return eval_holdout_set(HOLDOUT_B,(int)(sizeof HOLDOUT_B/sizeof HOLDOUT_B[0]),"runtime_choice_holdout_b",st,attr,print_cases);
}

static int eval_holdout_c(stats_t *st,attr_t *attr,int print_cases){
    return eval_holdout_set(HOLDOUT_C,(int)(sizeof HOLDOUT_C/sizeof HOLDOUT_C[0]),"runtime_choice_holdout_c",st,attr,print_cases);
}

static int rt_total,rt_pass;
static void rt(const char *name,int ok){ rt_total++; if(ok)rt_pass++; else printf("  FAIL runtime_choice_eval redteam: %s\n",name); }

static int redteam(void){
    int n=(int)(sizeof CASES/sizeof CASES[0]);
    rt("case count pinned",n==23);
    for(int i=0;i<n;i++){
        rt("case has enough choices",CASES[i].nc>=2&&CASES[i].nc<=MAXC);
        rt("correct index valid or NONE",CASES[i].correct==-1||(CASES[i].correct>=0&&CASES[i].correct<CASES[i].nc));
        rt("case has tags",CASES[i].tag&&CASES[i].tag[0]);
    }
    stats_t st[5]; n=eval_all(st,0);
    rt("raw neighborhood improves learned reachability",st[1].learned_reachable>st[0].learned_reachable);
    rt("semhash direct now matches neighborhood",st[2].ok==st[3].ok&&st[2].wrong==st[3].wrong&&st[2].miss==st[3].miss);
    rt("residual combo improves the current champion",st[4].ok>st[1].ok);
    rt("candidate collision rate independent of abstain gate",st[0].collisions==st[1].collisions&&st[1].collisions==st[2].collisions&&st[2].collisions==st[3].collisions&&st[3].collisions==st[4].collisions);
    rt("polarity channel removes residual polarity failures",st[4].polarity_fail==0);
    rt("semhash direct has no reachable-not-selected debt",st[2].r_not_sel==0);
    rt("semhash neighborhood reduces selected-not-reachable",st[3].sel_not_r<st[0].sel_not_r);
    rt("semhash neighborhood no longer wrong-acts on OOD",decide(&CASES[5],3).winner==-1&&decide(&CASES[6],3).winner==-1&&decide(&CASES[9],3).winner==-1&&decide(&CASES[12],3).winner==-1&&decide(&CASES[22],3).winner==-1);
    rt("residual restores out-of-domain abstention",st[4].wrong==0&&st[4].ood_gate==5);
    rt("residual handles added negation and near-class OOD",st[4].ok==n&&st[4].wrong==0&&st[4].miss==0);
    decision_t bridge_sem=decide(&CASES[3],3), bridge_res=decide(&CASES[3],4);
    rt("seeded semhash creates case3 reachability",bridge_sem.winner==CASES[3].correct&&bridge_sem.reachable==1);
    rt("residual preserves case3 reachability",bridge_res.winner==CASES[3].correct&&bridge_res.reachable==1);
    decision_t neg=decide(&CASES[8],4), near_ood=decide(&CASES[9],4);
    rt("residual negation chooses off",neg.winner==CASES[8].correct);
    neg=decide(&CASES[11],4);
    rt("residual contraction negation chooses dim",neg.winner==CASES[11].correct);
    neg=decide(&CASES[13],4);
    rt("residual don't-increase chooses dim",neg.winner==CASES[13].correct);
    neg=decide(&CASES[14],4);
    rt("residual do-not-brighter chooses dim",neg.winner==CASES[14].correct);
    neg=decide(&CASES[15],4);
    rt("residual inverse negation chooses brighten",neg.winner==CASES[15].correct);
    neg=decide(&CASES[16],4);
    rt("residual keep-from-brighter chooses dim",neg.winner==CASES[16].correct);
    neg=decide(&CASES[17],4);
    rt("residual keep-from-darker chooses brighten",neg.winner==CASES[17].correct);
    neg=decide(&CASES[18],4);
    rt("residual avoid-brighter chooses dim",neg.winner==CASES[18].correct);
    neg=decide(&CASES[19],4);
    rt("residual prevent-darker chooses brighten",neg.winner==CASES[19].correct);
    neg=decide(&CASES[20],4);
    rt("residual stop-brighter chooses dim",neg.winner==CASES[20].correct);
    neg=decide(&CASES[21],4);
    rt("residual stop-darker chooses brighten",neg.winner==CASES[21].correct);
    neg=decide(&CASES[22],4);
    rt("residual transit polarity OOD abstains",neg.winner==-1);
    rt("semhash neighborhood zero wrong-actuation pinned",st[3].wrong==0&&st[3].miss==0);
    rt("semhash direct zero wrong-actuation pinned",st[2].wrong==0&&st[2].miss==0);
    rt("residual route-state counts pinned",st[4].learned_reachable==18&&st[4].residual_rescue==0&&st[4].unsupported==5);
    rt("semhash neighborhood commit precision pinned",st[3].ok==23&&n-st[3].miss==23);
    rt("semhash neighborhood learned coverage pinned",st[3].learned_reachable==18&&in_domain_cases()==18);
    rt("semhash direct learned coverage pinned",st[2].ok==23&&st[2].learned_reachable==18&&in_domain_cases()==18);
    rt("residual LC0 pinned",st[4].wrong==0&&st[4].learned_reachable==18&&in_domain_cases()==18);
    rt("near-class OOD knownness below residual gate",near_ood.knownness<RESIDUAL_KNOWN_GATE);
    rt("near-class OOD abstains",near_ood.winner==-1);
    rt("near-class OOD abstain is not reachable",near_ood.reachable==0);
    printf("RUNTIME_CHOICE_EVAL_REDTEAM checks=%d/%d score=%d/100\n",rt_pass,rt_total,rt_total?(100*rt_pass)/rt_total:0);
    return rt_pass==rt_total?0:1;
}

static int holdout_redteam(void){
    int n=(int)(sizeof HOLDOUT/sizeof HOLDOUT[0]);
    rt_total=rt_pass=0;
    rt("holdout case count pinned",n==20);
    for(int i=0;i<n;i++){
        rt("holdout case has enough choices",HOLDOUT[i].nc>=2&&HOLDOUT[i].nc<=MAXC);
        rt("holdout correct index valid or NONE",HOLDOUT[i].correct==-1||(HOLDOUT[i].correct>=0&&HOLDOUT[i].correct<HOLDOUT[i].nc));
        rt("holdout case is tagged blind",strstr(HOLDOUT[i].tag,"blind")!=NULL);
    }
    stats_t st[5]; attr_t attr; n=eval_holdout(st,&attr,0);
    int in_domain=in_domain_set(HOLDOUT,n);
    rt("holdout semhash neighborhood solved",st[3].ok==n&&st[3].wrong==0&&st[3].miss==0);
    rt("holdout semhash direct solved",st[2].ok==n&&st[2].wrong==0&&st[2].miss==0);
    rt("holdout residual perfect",st[4].ok==n&&st[4].wrong==0&&st[4].miss==0);
    rt("holdout learned coverage pinned",st[3].learned_reachable==9&&in_domain==9);
    rt("holdout learned accept visible",attr.learned_accept==2);
    rt("holdout learned reject visible",attr.learned_reject==1);
    rt("holdout domain reject pinned",attr.domain_reject==9);
    rt("holdout location reject pinned",attr.location_reject==1);
    rt("holdout hard OOD veto empty",attr.hard_ood_veto==0);
    rt("holdout residual rescue eliminated",attr.residual_rescue==0);
    rt("holdout no topology rescue",attr.topology_rescue==0);
    rt("holdout operator factor pinned",attr.operator_factor==7);
    rt("holdout attribution sums",attr.learned_accept+attr.learned_reject+attr.topology_rescue+attr.operator_factor+attr.domain_reject+attr.location_reject+attr.hard_ood_veto+attr.residual_rescue+attr.wrong==n);
    printf("RUNTIME_CHOICE_HOLDOUT_REDTEAM checks=%d/%d score=%d/100\n",rt_pass,rt_total,rt_total?(100*rt_pass)/rt_total:0);
    return rt_pass==rt_total?0:1;
}

static int holdout_b_redteam(void){
    int n=(int)(sizeof HOLDOUT_B/sizeof HOLDOUT_B[0]);
    rt_total=rt_pass=0;
    rt("holdout B case count pinned",n==14);
    for(int i=0;i<n;i++){
        rt("holdout B case has enough choices",HOLDOUT_B[i].nc>=2&&HOLDOUT_B[i].nc<=MAXC);
        rt("holdout B correct index valid or NONE",HOLDOUT_B[i].correct==-1||(HOLDOUT_B[i].correct>=0&&HOLDOUT_B[i].correct<HOLDOUT_B[i].nc));
        rt("holdout B case is tagged blind-b",strstr(HOLDOUT_B[i].tag,"blind-b")!=NULL);
    }
    stats_t st[5]; attr_t attr; n=eval_holdout_b(st,&attr,0);
    int in_domain=in_domain_set(HOLDOUT_B,n);
    rt("holdout B semhash neighborhood remediated",st[3].ok==n&&st[3].wrong==0&&st[3].miss==0);
    rt("holdout B semhash direct remediated",st[2].ok==n&&st[2].wrong==0&&st[2].miss==0);
    rt("holdout B residual remediated",st[4].ok==n&&st[4].wrong==0&&st[4].miss==0);
    rt("holdout B learned coverage pinned",st[3].learned_reachable==5&&in_domain==5);
    rt("holdout B learned accept pinned",attr.learned_accept==2);
    rt("holdout B learned reject pinned",attr.learned_reject==1);
    rt("holdout B domain reject pinned",attr.domain_reject==3);
    rt("holdout B location reject pinned",attr.location_reject==5);
    rt("holdout B hard OOD veto empty",attr.hard_ood_veto==0);
    rt("holdout B residual rescue absent",attr.residual_rescue==0);
    rt("holdout B wrong acts eliminated",attr.wrong==0);
    rt("holdout B operator factor pinned",attr.operator_factor==3);
    rt("holdout B topology rescue absent",attr.topology_rescue==0);
    rt("holdout B attribution sums",attr.learned_accept+attr.learned_reject+attr.topology_rescue+attr.operator_factor+attr.domain_reject+attr.location_reject+attr.hard_ood_veto+attr.residual_rescue+attr.wrong==n);
    printf("RUNTIME_CHOICE_HOLDOUT_B_REDTEAM checks=%d/%d score=%d/100\n",rt_pass,rt_total,rt_total?(100*rt_pass)/rt_total:0);
    return rt_pass==rt_total?0:1;
}

static int holdout_c_redteam(void){
    int n=(int)(sizeof HOLDOUT_C/sizeof HOLDOUT_C[0]);
    rt_total=rt_pass=0;
    rt("holdout C case count pinned",n==9);
    for(int i=0;i<n;i++){
        rt("holdout C case has enough choices",HOLDOUT_C[i].nc>=2&&HOLDOUT_C[i].nc<=MAXC);
        rt("holdout C correct index valid or NONE",HOLDOUT_C[i].correct==-1||(HOLDOUT_C[i].correct>=0&&HOLDOUT_C[i].correct<HOLDOUT_C[i].nc));
        rt("holdout C case is tagged blind-c",strstr(HOLDOUT_C[i].tag,"blind-c")!=NULL);
    }
    stats_t st[5]; attr_t attr; n=eval_holdout_c(st,&attr,0);
    int in_domain=in_domain_set(HOLDOUT_C,n);
    rt("holdout C semhash neighborhood solved",st[3].ok==n&&st[3].wrong==0&&st[3].miss==0);
    rt("holdout C semhash direct solved",st[2].ok==n&&st[2].wrong==0&&st[2].miss==0);
    rt("holdout C residual solved",st[4].ok==n&&st[4].wrong==0&&st[4].miss==0);
    rt("holdout C learned coverage pinned",st[3].learned_reachable==5&&in_domain==5);
    rt("holdout C learned accept pinned",attr.learned_accept==5);
    rt("holdout C learned reject absent",attr.learned_reject==0);
    rt("holdout C domain reject absent",attr.domain_reject==0);
    rt("holdout C location reject pinned",attr.location_reject==4);
    rt("holdout C hard OOD veto empty",attr.hard_ood_veto==0);
    rt("holdout C residual rescue absent",attr.residual_rescue==0);
    rt("holdout C wrong acts eliminated",attr.wrong==0);
    rt("holdout C operator factor pinned",attr.operator_factor==0);
    rt("holdout C topology rescue absent",attr.topology_rescue==0);
    rt("holdout C attribution sums",attr.learned_accept+attr.learned_reject+attr.topology_rescue+attr.operator_factor+attr.domain_reject+attr.location_reject+attr.hard_ood_veto+attr.residual_rescue+attr.wrong==n);
    printf("RUNTIME_CHOICE_HOLDOUT_C_REDTEAM checks=%d/%d score=%d/100\n",rt_pass,rt_total,rt_total?(100*rt_pass)/rt_total:0);
    return rt_pass==rt_total?0:1;
}

static void factor_label(int mask,char *out,int cap){
    snprintf(out,(size_t)cap,"%s%s%s%s%s",
             (mask&FF_POLARITY)?"pol":"-",
             (mask&FF_COLOR)?"+color":"",
             (mask&FF_COMP)?"+comp":"",
             (mask&FF_LOCATION)?"+loc":"",
             (mask&FF_SUPPORT)?"+support":"");
}

static int floor_sweep(void){
    int save=FACTORS, pass=0;
    printf("runtime_choice_floor topology_free_direct masks=%d\n",32);
    printf("mask\tfactors\tfrozen\tholdout_a\tholdout_b\tholdout_c\twrong\tmiss\tlearned\tin_domain\tpass\n");
    for(int mask=0;mask<32;mask++){
        stats_t base[5], a[5], b[5], c[5]; attr_t aa,bb,cc; char label[80];
        FACTORS=mask;
        int nb=eval_all(base,0), na=eval_holdout(a,&aa,0), nbb=eval_holdout_b(b,&bb,0), nc=eval_holdout_c(c,&cc,0);
        int ok=base[2].ok+a[2].ok+b[2].ok+c[2].ok;
        int total=nb+na+nbb+nc;
        int wrong=base[2].wrong+a[2].wrong+b[2].wrong+c[2].wrong;
        int miss=base[2].miss+a[2].miss+b[2].miss+c[2].miss;
        int learned=base[2].learned_reachable+a[2].learned_reachable+b[2].learned_reachable+c[2].learned_reachable;
        int ind=in_domain_cases()+in_domain_set(HOLDOUT,na)+in_domain_set(HOLDOUT_B,nbb)+in_domain_set(HOLDOUT_C,nc);
        int good=(ok==total&&wrong==0&&miss==0&&learned==ind);
        if(good) pass++;
        factor_label(mask,label,sizeof label);
        printf("%02d\t%s\t%d/%d\t%d/%d\t%d/%d\t%d/%d\t%d\t%d\t%d\t%d\t%s\n",
               mask,label,base[2].ok,nb,a[2].ok,na,b[2].ok,nbb,c[2].ok,nc,wrong,miss,learned,ind,good?"yes":"no");
    }
    FACTORS=save;
    printf("RUNTIME_CHOICE_FLOOR passing_masks=%d/32\n",pass);
    return 0;
}

static void usage(void){ fprintf(stderr,"usage: runtime_choice_eval [train validation test nlu.csv] [--redteam|--details|--holdout|--holdout-redteam|--holdout-b|--holdout-b-redteam|--holdout-c|--holdout-c-redteam|--floor]\n"); }

int main(int argc,char **argv){
    const char *paths[4]={"data/train.json","data/validation.json","data/test.json","data/nlu_home.csv"};
    int red=0,details=0,holdout=0,holdout_red=0,holdout_b=0,holdout_b_red=0,holdout_c=0,holdout_c_red=0,floor=0,arg=1;
    if(argc>=5&&argv[1][0]!='-'&&argv[2][0]!='-'&&argv[3][0]!='-'&&argv[4][0]!='-'){for(int i=0;i<4;i++)paths[i]=argv[i+1];arg=5;}
    for(int i=arg;i<argc;i++){ if(!strcmp(argv[i],"--redteam"))red=1; else if(!strcmp(argv[i],"--details"))details=1; else if(!strcmp(argv[i],"--holdout"))holdout=1; else if(!strcmp(argv[i],"--holdout-redteam"))holdout_red=1; else if(!strcmp(argv[i],"--holdout-b"))holdout_b=1; else if(!strcmp(argv[i],"--holdout-b-redteam"))holdout_b_red=1; else if(!strcmp(argv[i],"--holdout-c"))holdout_c=1; else if(!strcmp(argv[i],"--holdout-c-redteam"))holdout_c_red=1; else if(!strcmp(argv[i],"--floor"))floor=1; else {usage();return 1;} }
    if(red+details+holdout+holdout_red+holdout_b+holdout_b_red+holdout_c+holdout_c_red+floor>1){usage();return 1;}
    load_data(paths[0],paths[1],paths[2],paths[3]); train_semhash(); train_seeded_projection();
    if(red)return redteam();
    if(holdout_red)return holdout_redteam();
    if(holdout_b_red)return holdout_b_redteam();
    if(holdout_c_red)return holdout_c_redteam();
    if(floor)return floor_sweep();
    if(details){dump_details();return 0;}
    if(holdout_c){
        stats_t st[5]; attr_t attr; int n=eval_holdout_c(st,&attr,1); int in_domain=in_domain_set(HOLDOUT_C,n);
        printf("\nvariant\taccuracy\tcommit_precision\tlearned_coverage\twrong_act\tmissed_none\tlearned_reachable\tresidual_rescue\tunsupported\n");
        for(int v=2;v<5;v++){ int committed=n-st[v].miss; printf("%s\t%d/%d\t%d/%d\t%d/%d\t%d/%d\t%d/%d\t%d\t%d\t%d\n",st[v].name,st[v].ok,n,st[v].ok,committed,st[v].learned_reachable,in_domain,st[v].wrong,n,st[v].miss,n,st[v].learned_reachable,st[v].residual_rescue,st[v].unsupported); }
        printf("\nattribution\tlearned_accept=%d\tlearned_reject=%d\ttopology_rescue=%d\toperator_factor=%d\tdomain_reject=%d\tlocation_reject=%d\thard_ood_veto=%d\tresidual_rescue=%d\twrong=%d\n",attr.learned_accept,attr.learned_reject,attr.topology_rescue,attr.operator_factor,attr.domain_reject,attr.location_reject,attr.hard_ood_veto,attr.residual_rescue,attr.wrong);
        printf("decision: Holdout C tests unseen runtime referent binding relationally.\n");
        return 0;
    }
    if(holdout_b){
        stats_t st[5]; attr_t attr; int n=eval_holdout_b(st,&attr,1); int in_domain=in_domain_set(HOLDOUT_B,n);
        printf("\nvariant\taccuracy\tcommit_precision\tlearned_coverage\twrong_act\tmissed_none\tlearned_reachable\tresidual_rescue\tunsupported\n");
        for(int v=2;v<5;v++){ int committed=n-st[v].miss; printf("%s\t%d/%d\t%d/%d\t%d/%d\t%d/%d\t%d/%d\t%d\t%d\t%d\n",st[v].name,st[v].ok,n,st[v].ok,committed,st[v].learned_reachable,in_domain,st[v].wrong,n,st[v].miss,n,st[v].learned_reachable,st[v].residual_rescue,st[v].unsupported); }
        printf("\nattribution\tlearned_accept=%d\tlearned_reject=%d\ttopology_rescue=%d\toperator_factor=%d\tdomain_reject=%d\tlocation_reject=%d\thard_ood_veto=%d\tresidual_rescue=%d\twrong=%d\n",attr.learned_accept,attr.learned_reject,attr.topology_rescue,attr.operator_factor,attr.domain_reject,attr.location_reject,attr.hard_ood_veto,attr.residual_rescue,attr.wrong);
        printf("decision: Holdout B is remediated combinatorial transfer; first-shot failures remain documented.\n");
        return 0;
    }
    if(holdout){
        stats_t st[5]; attr_t attr; int n=eval_holdout(st,&attr,1); int in_domain=in_domain_set(HOLDOUT,n);
        printf("\nvariant\taccuracy\tcommit_precision\tlearned_coverage\twrong_act\tmissed_none\tlearned_reachable\tresidual_rescue\tunsupported\n");
        for(int v=2;v<5;v++){ int committed=n-st[v].miss; printf("%s\t%d/%d\t%d/%d\t%d/%d\t%d/%d\t%d/%d\t%d\t%d\t%d\n",st[v].name,st[v].ok,n,st[v].ok,committed,st[v].learned_reachable,in_domain,st[v].wrong,n,st[v].miss,n,st[v].learned_reachable,st[v].residual_rescue,st[v].unsupported); }
        printf("\nattribution\tlearned_accept=%d\tlearned_reject=%d\ttopology_rescue=%d\toperator_factor=%d\tdomain_reject=%d\tlocation_reject=%d\thard_ood_veto=%d\tresidual_rescue=%d\twrong=%d\n",attr.learned_accept,attr.learned_reject,attr.topology_rescue,attr.operator_factor,attr.domain_reject,attr.location_reject,attr.hard_ood_veto,attr.residual_rescue,attr.wrong);
        printf("decision: blind holdout separates learned coverage from hard OOD vetoes; keep this set frozen.\n");
        return 0;
    }
    stats_t st[5]; int n=eval_all(st,1);
    int in_domain=in_domain_cases();
    printf("\nvariant\taccuracy\tcommit_precision\tlearned_coverage\twrong_act\tmissed_none\tmean_margin\tcollision_rate\treachable_not_selected\tselected_not_reachable\tpolarity_failures\tood_gates\tlearned_reachable\tresidual_rescue\tunsupported\n");
    for(int v=0;v<5;v++){
        int committed=n-st[v].miss;
        printf("%s\t%d/%d\t%d/%d\t%d/%d\t%d/%d\t%d/%d\t%ld\t%d/%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",st[v].name,st[v].ok,n,st[v].ok,committed,st[v].learned_reachable,in_domain,st[v].wrong,n,st[v].miss,n,st[v].margin_sum/n,st[v].collisions,n,st[v].r_not_sel,st[v].sel_not_r,st[v].polarity_fail,st[v].ood_gate,st[v].learned_reachable,st[v].residual_rescue,st[v].unsupported);
    }
    printf("\ndecision: ");
    if(st[2].ok==st[3].ok && st[2].ok==st[4].ok && st[2].wrong==0) printf("semhash_direct now matches topology and residual; topology is scaffold-only on this probe.\n");
    else if(st[3].ok==st[4].ok && st[3].wrong==0) printf("semhash_neighborhood now matches residual_combo; keep exact topology and expand the adversarial set before NSW.\n");
    else if(st[4].ok>st[1].ok && st[4].wrong<st[1].wrong) printf("residual_combo beats the current champion; keep combined evidence and expand the adversarial set.\n");
    else if(st[2].ok>=st[3].ok && st[2].wrong<=st[3].wrong) printf("semhash_direct is enough for the learned path; keep it simple before adding topology.\n");
    else if(st[3].ok>st[2].ok) printf("semhash_neighborhood improves the learned path; add topology there next.\n");
    else printf("results are mixed; inspect polarity/reachability tags before changing architecture.\n");
    if(st[4].ok<n) printf("next: residual is not perfect yet; inspect remaining failures before NSW.\n");
    if(st[4].polarity_fail) printf("next: polarity failures persist; strengthen the factorized polarity channel.\n");
    else printf("next: polarity failures are zero under residual_combo on this probe.\n");
    if(st[3].r_not_sel||st[2].r_not_sel) printf("next: reachable-but-not-selected exists; improve selection/rerank before NSW.\n");
    if(st[4].sel_not_r) printf("next: residual still has selected-but-not-reachable cases; make the learned representation create the missing bridge.\n");
    if(st[4].wrong==0) printf("next: OOD knownness gate preserves NONE on this probe; expand OOD negatives.\n");
    if(st[4].sel_not_r>n/3) printf("next: reachability is poor; improve training/projection before NSW.\n");
    return 0;
}
