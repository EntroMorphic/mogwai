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
#define K 8

typedef struct { int idx, score; } near_t;
typedef struct { uint64_t code; int pred, score; } sh_t;
typedef struct { const char *query, *tag; int correct, nc; const char *choice[MAXC]; } testcase_t;
typedef struct { int winner, score, second, margin, reachable, collision, knownness; } decision_t;
typedef struct { int direct, overlap, hist, raw_topo, sem, code, qpol, cpol, pcompat, reachable; } atom_t;
typedef struct { const char *name; int ok, wrong, miss, collisions, r_not_sel, sel_not_r, polarity_fail, ood_gate; long margin_sum; } stats_t;

static char *U_t[MAXU]; static char U_l[MAXU][RNAMELEN]; static int U_n;
static char *V_t[3000]; static char V_l[3000][RNAMELEN]; static int V_n;
static char *T_t[4000]; static int T_n;
static router_t R; static tvec *TI; static uint16_t *ACT; static int32_t CW[RMAXCLS][RD];
static prune_opt PRUNE = {0,0,0,RSHIP_NEGTOP,0,0};

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
     {"turn the lights off", "change the lights to red", "issue a rail refund"}}
};

static char *xstrdup(const char *s){ char *p=strdup(s); if(!p){fprintf(stderr,"out of memory\n");exit(1);} return p; }
static int js(const char*l,const char*k,char*o,int cap){
    char pat[64]; snprintf(pat,sizeof pat,"\"%s\":",k); const char*p=strstr(l,pat); if(!p)return 0; p+=strlen(pat);
    while(*p==' ')p++; if(*p!='\"')return 0; p++; int n=0;
    while(*p&&*p!='\"'&&n<cap-1){ if(*p=='\\'&&p[1])p++; o[n++]=*p++; } o[n]=0; return 1;
}
static int isiot(const char*l){return !strncmp(l,"iot_",4);}

#define HN 65536
static char *HS[HN];
static void hs_add(const char*s){ char b[512]; r_norm(s,b,sizeof b); uint32_t h=r_fnv(b,(int)strlen(b))%HN;
    for(int n=0;HS[h]&&n<HN;n++,h=(h+1)%HN) if(!strcmp(HS[h],b))return;
    if(HS[h]){fprintf(stderr,"hash set full\n");exit(1);} HS[h]=xstrdup(b); }
static int hs_has(const char*s){ char b[512]; r_norm(s,b,sizeof b); uint32_t h=r_fnv(b,(int)strlen(b))%HN;
    for(int n=0;HS[h]&&n<HN;n++,h=(h+1)%HN) if(!strcmp(HS[h],b))return 1; return 0; }
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
        if(nf<10)continue; for(int i=0;i<nf;i++){char*s=fl[i];int L=(int)strlen(s); while(L&&(s[L-1]=='\n'||s[L-1]=='\r'))s[--L]=0; if(L>=2&&s[0]=='\"'&&s[L-1]=='\"'){s[L-1]=0;fl[i]=s+1;}}
        if(strcmp(fl[2],"iot")||hs_has(fl[9]))continue; char lb[RNAMELEN]; snprintf(lb,sizeof lb,"iot_%s",fl[3]); push(U_t,U_l,&U_n,MAXU,fl[9],lb); hs_add(fl[9]);
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

static int polarity(const char *text){
    int up=0,down=0;
    int neg=has_word(text,"not")||has_word(text,"dont")||has_word(text,"don't")||has_word(text,"do not")||has_word(text,"never");
    const char *ups[]={"increase","raise","brighter","brighten","bright","on",NULL};
    const char *downs[]={"decrease","lower","dim","dimmer","dark","darker","darken","less","off",NULL};
    for(int i=0;ups[i];i++) if(has_word(text,ups[i])) up=1;
    for(int i=0;downs[i];i++) if(has_word(text,downs[i])) down=1;
    if(neg){ int t=up; up=down; down=t; }
    if(up&&!down) return 1;
    if(down&&!up) return -1;
    return 0;
}

static int polarity_score(int qp,int cp){
    if(!qp||!cp) return 0;
    return qp==cp ? 80 : -120;
}

static const char *variant_name(int variant){
    static const char *n[]={"raw_direct","raw_neighborhood","semhash_direct","semhash_neighborhood","residual_combo"};
    return n[variant];
}

static int dot_cls(int c,const int16_t *acc){ int64_t s=0; for(int d=0;d<RD;d++) if(acc[d])s+=(int64_t)CW[c][d]*acc[d]; if(s>2147483647LL)return 2147483647; if(s<-2147483647LL)return -2147483647; return (int)s; }
static void train_semhash(void){ int16_t acc[RD]; int32_t tot; (void)tot; for(int ep=0;ep<EPOCHS;ep++){ int err=0; for(int i=0;i<U_n;i++){ r_counts(U_t[i],acc,&tot); int y=R.label[i],pred=0,best=dot_cls(0,acc); for(uint32_t c=1;c<R.n_class;c++){int s=dot_cls((int)c,acc); if(s>best){best=s;pred=(int)c;}} if(pred!=y){err++; for(int d=0;d<RD;d++)if(acc[d]){CW[y][d]+=acc[d]; CW[pred][d]-=acc[d];}} } if(!err)break; } }
static int code_bit(int cls,int j){ char b[80]; snprintf(b,sizeof b,"%s#%d",R.names[cls],j); uint32_t h=r_fnv(b,(int)strlen(b)); return (((h>>16)^h)&1)?1:-1; }
static uint64_t class_code(int cls){ uint64_t c=0; for(int j=0;j<HD;j++)if(code_bit(cls,j)>0)c|=1ull<<j; return c; }
static int pop64(uint64_t x){ return __builtin_popcountll(x); }
static int code_score(uint64_t a,uint64_t b){ return (HD-2*pop64(a^b))*256/HD; }
static sh_t sem_encode(const char *text){ int16_t acc[RD]; int32_t tot; (void)tot; r_counts(text,acc,&tot); int best=-(1<<28),bi=0; for(uint32_t c=0;c<R.n_class;c++){int s=dot_cls((int)c,acc); if(s>best){best=s;bi=(int)c;}} sh_t out={class_code(bi),bi,best}; return out; }
static int sem_sim(sh_t q,sh_t c){ return code_score(q.code,c.code)+c.score/8; }

static decision_t decide(const testcase_t *tc,int variant){
    tvec qv,cv[MAXC]; int qa,ca[MAXC],qh[RMAXCLS],ch[MAXC][RMAXCLS]; near_t qn[K],cn[MAXC][K];
    sh_t q=sem_encode(tc->query), cs[MAXC]; topk(tc->query,qn,K,&qv,&qa); hist(qn,K,qh);
    decision_t d={-1,-(1<<28),-(1<<28),0,0,0,0}; int correct_reachable=0;
    int q_top=R.label[qn[0].idx];
    int q_known=0; for(int i=0;i<K;i++)q_known+=qn[i].score; q_known/=K;
    d.knownness=q_known;
    int q_none = (variant<2 || variant==4) ? !strcmp(R.names[q_top],"none") : !strcmp(R.names[q.pred],"none");
    int qp=polarity(tc->query);
    for(int i=0;i<tc->nc;i++){
        topk(tc->choice[i],cn[i],K,&cv[i],&ca[i]); hist(cn[i],K,ch[i]); cs[i]=sem_encode(tc->choice[i]);
        int direct=t_score_pre(&qv,&cv[i],qa,ca[i]); int ov=overlap(qn,cn[i],K); int hd=hist_dot(qh,ch[i]); int code=code_score(q.code,cs[i].code);
        int raw_topo = direct + 4*ov + hd/10;
        int sem = sem_sim(q,cs[i]);
        int score = direct;
        if(variant==1) score = raw_topo;
        else if(variant==2) score = sem;
        else if(variant==3) score = sem + 4*ov + hd/10;
        else if(variant==4) score = raw_topo + sem/4 + code/8 + polarity_score(qp,polarity(tc->choice[i]));
        int reach = (variant<2) ? (ov>0 || hd>=500) : (code>0 || ov>0 || hd>=500);
        if(i==tc->correct) correct_reachable=reach;
        if(!q_none){
            if(score>d.score){d.second=d.score; d.score=score; d.winner=i; d.reachable=reach;} else if(score>d.second)d.second=score;
        }
    }
    for(int i=0;i<tc->nc;i++)for(int j=i+1;j<tc->nc;j++){ int s=t_score_pre(&cv[i],&cv[j],ca[i],ca[j]); if(s>=COLLISION_WARN)d.collision=1; }
    if(q_none){ d.score=0; d.second=0; return d; }
    d.margin=d.score-d.second;
    if(variant==4 && q_known<RESIDUAL_KNOWN_GATE){ d.winner=-1; d.score=0; d.second=0; d.margin=0; d.reachable=0; return d; }
    if(tc->correct>=0 && correct_reachable && d.winner!=tc->correct)d.reachable=2;
    return d;
}

static void tally(stats_t *s,const testcase_t *tc,decision_t d){
    s->margin_sum+=d.margin; if(d.collision)s->collisions++; if(d.winner<0)s->ood_gate++;
    if(tc->correct<0){ if(d.winner<0)s->ok++; else s->wrong++; }
    else if(d.winner==tc->correct)s->ok++; else if(d.winner<0)s->miss++; else s->wrong++;
    if(strstr(tc->tag,"polarity") && d.winner!=tc->correct)s->polarity_fail++;
    if(d.reachable==2)s->r_not_sel++; else if(d.winner>=0 && !d.reachable)s->sel_not_r++;
}

static void init_stats(stats_t *st){
    memset(st,0,5*sizeof st[0]);
    for(int i=0;i<5;i++) st[i].name=variant_name(i);
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
            decision_t d=decide(tc,v); int q_none=(v<2||v==4)?!strcmp(R.names[qtop],"none"):!strcmp(R.names[q.pred],"none");
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

static int rt_total,rt_pass;
static void rt(const char *name,int ok){ rt_total++; if(ok)rt_pass++; else printf("  FAIL runtime_choice_eval redteam: %s\n",name); }

static int redteam(void){
    int n=(int)(sizeof CASES/sizeof CASES[0]);
    rt("case count pinned",n==10);
    for(int i=0;i<n;i++){
        rt("case has enough choices",CASES[i].nc>=2&&CASES[i].nc<=MAXC);
        rt("correct index valid or NONE",CASES[i].correct==-1||(CASES[i].correct>=0&&CASES[i].correct<CASES[i].nc));
        rt("case has tags",CASES[i].tag&&CASES[i].tag[0]);
    }
    stats_t st[5]; n=eval_all(st,0);
    rt("raw neighborhood improves raw direct",st[1].ok>st[0].ok&&st[1].wrong<st[0].wrong);
    rt("semhash neighborhood improves semhash direct",st[3].ok>st[2].ok&&st[3].wrong<st[2].wrong);
    rt("residual combo improves the current champion",st[4].ok>st[1].ok);
    rt("candidate collision rate independent of abstain gate",st[0].collisions==st[1].collisions&&st[1].collisions==st[2].collisions&&st[2].collisions==st[3].collisions&&st[3].collisions==st[4].collisions);
    rt("polarity channel removes residual polarity failures",st[4].polarity_fail==0);
    rt("reachable-not-selected still visible before NSW",st[2].r_not_sel>0);
    rt("semhash neighborhood reduces selected-not-reachable",st[3].sel_not_r<st[0].sel_not_r);
    rt("semhash out-of-domain wrong acts still counted",st[3].wrong>0);
    rt("residual restores out-of-domain abstention",st[4].wrong==0&&st[4].ood_gate==3);
    rt("residual handles added negation and near-class OOD",st[4].ok==n&&st[4].wrong==0&&st[4].miss==0);
    decision_t neg=decide(&CASES[8],4), near_ood=decide(&CASES[9],4);
    rt("residual negation chooses off",neg.winner==CASES[8].correct);
    rt("near-class OOD knownness below residual gate",near_ood.knownness<RESIDUAL_KNOWN_GATE);
    rt("near-class OOD abstains",near_ood.winner==-1);
    rt("near-class OOD abstain is not reachable",near_ood.reachable==0);
    printf("RUNTIME_CHOICE_EVAL_REDTEAM checks=%d/%d score=%d/100\n",rt_pass,rt_total,rt_total?(100*rt_pass)/rt_total:0);
    return rt_pass==rt_total?0:1;
}

static void usage(void){ fprintf(stderr,"usage: runtime_choice_eval [train validation test nlu.csv] [--redteam|--details]\n"); }

int main(int argc,char **argv){
    const char *paths[4]={"data/train.json","data/validation.json","data/test.json","data/nlu_home.csv"};
    int red=0,details=0,arg=1;
    if(argc>=5&&argv[1][0]!='-'&&argv[2][0]!='-'&&argv[3][0]!='-'&&argv[4][0]!='-'){for(int i=0;i<4;i++)paths[i]=argv[i+1];arg=5;}
    for(int i=arg;i<argc;i++){ if(!strcmp(argv[i],"--redteam"))red=1; else if(!strcmp(argv[i],"--details"))details=1; else {usage();return 1;} }
    if(red&&details){usage();return 1;}
    load_data(paths[0],paths[1],paths[2],paths[3]); train_semhash();
    if(red)return redteam();
    if(details){dump_details();return 0;}
    stats_t st[5]; int n=eval_all(st,1);
    printf("\nvariant\taccuracy\twrong_act\tmissed_none\tmean_margin\tcollision_rate\treachable_not_selected\tselected_not_reachable\tpolarity_failures\tood_gates\n");
    for(int v=0;v<5;v++) printf("%s\t%d/%d\t%d/%d\t%d/%d\t%ld\t%d/%d\t%d\t%d\t%d\t%d\n",st[v].name,st[v].ok,n,st[v].wrong,n,st[v].miss,n,st[v].margin_sum/n,st[v].collisions,n,st[v].r_not_sel,st[v].sel_not_r,st[v].polarity_fail,st[v].ood_gate);
    printf("\ndecision: ");
    if(st[4].ok>st[1].ok && st[4].wrong<st[1].wrong) printf("residual_combo beats the current champion; keep combined evidence and expand the adversarial set.\n");
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
