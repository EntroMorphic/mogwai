/* cascade_eval.c — two-stage cascade for 60-class NN: does reranking by
 * word-set Dice recover the errors the character n-gram makes?
 *
 * The error dump showed three failure classes:
 *   - common-phrase collision: "number of people" matches email, not lookup
 *   - politeness frame dominance: "could you...please" swamps content words
 *   - short utterances: few n-grams, Dice dominated by coincidence
 *
 * Word-set Dice sees whole words and is not fooled by phrase overlap
 * inside a longer utterance. The cascade: stage 1 (twin-ternary) produces
 * a shortlist of K; stage 2 reranks K by c_text_sim. The question is
 * whether the rerank changes the argmax to the correct class.
 *
 * No float on the hot path. int32_t is the widest type. No Python. */
#include "router.h"
#include "ternary.h"
#include "cascade.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MC_MAXCLS 128

typedef struct { char text[512]; char label[RNAMELEN]; } ex;
static ex *Tr=0,*Te=0; static int Tr_n,Tr_cap,Te_n,Te_cap;

static void push(ex **a,int *n,int *c,const char *t,const char *l){
    if(*n==*c){*c=*c?*c*2:4096;*a=realloc(*a,*c*sizeof(ex));}
    strncpy((*a)[*n].text,t,511);(*a)[*n].text[511]=0;
    strncpy((*a)[*n].label,l,RNAMELEN-1);(*a)[*n].label[RNAMELEN-1]=0;(*n)++;
}
static int js(const char *line,const char *key,char *out,int sz){
    char pat[32];
    int pl=snprintf(pat,sizeof pat,"\"%s\":",key);
    const char *p=strstr(line,pat);
    if(!p) return 0;
    p+=pl;
    while(*p==' '||*p=='\t') p++;
    if(*p != '"') return 0;
    p++;
    int i=0;
    while(*p && *p != '"' && i<sz-1) {
        if(*p=='\\' && p[1]) p++;
        out[i++]=*p++;
    }
    out[i]=0;
    return 1;
}

int main(int argc,char**argv){
    if(argc<4){fprintf(stderr,"usage: cascade_eval <train.json> <test.json> <K>\n");return 1;}
    int K=atoi(argv[3]);
    FILE *f=fopen(argv[1],"r");
    char line[8192],t[512],l[RNAMELEN];
    while(fgets(line,sizeof line,f))
        if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l))
            push(&Tr,&Tr_n,&Tr_cap,t,l);
    fclose(f);
    f=fopen(argv[2],"r");
    while(fgets(line,sizeof line,f))
        if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l))
            push(&Te,&Te_n,&Te_cap,t,l);
    fclose(f);
    fprintf(stderr,"  train %d, test %d, K=%d\n",Tr_n,Te_n,K);

    router_t r;memset(&r,0,sizeof r);r.dim=RD;
    {int32_t *acc=calloc(RD,sizeof(int32_t));int64_t total=0;int16_t c[RD];
     for(int i=0;i<Tr_n;i++){int32_t tt;r_counts(Tr[i].text,c,&tt);total+=tt;for(int d=0;d<RD;d++)acc[d]+=c[d];}
     for(int d=0;d<RD;d++)r.centre[d]=(int32_t)(total>0?(acc[d]*(int64_t)RSCALE)/total:0);
     free(acc);}

    t_popcnt_init();
    tvec *T_idx=malloc(Tr_n*sizeof(tvec));int *T_act=malloc(Tr_n*sizeof(int));
    for(int i=0;i<Tr_n;i++){t_encode(&r,Tr[i].text,&T_idx[i]);T_act[i]=t_active(&T_idx[i]);}

    /* for each test item: stage 1 shortlist, stage 2 rerank */
    int twin_only_ok=0,cascade_ok=0;
    int changed=0,changed_correct=0,changed_wrong=0;

    /* also track: was the correct answer in the shortlist? */
    int in_shortlist=0;

    for(int q=0;q<Te_n;q++){
        tvec qv;t_encode(&r,Te[q].text,&qv);int qa=t_active(&qv);

        /* stage 1: score all, keep top K */
        /* store (score, index) pairs */
        typedef struct{int s,i;} si;
        si *all=malloc(Tr_n*sizeof(si));
        for(int i=0;i<Tr_n;i++){all[i].s=t_score_pre(&qv,&T_idx[i],qa,T_act[i]);all[i].i=i;}
        /* partial selection: find top K via nth_element-style */
        /* simple: sort is fine for a dev probe (n=11k, not the device) */
        /* but a full sort is O(n log n) per query; use a min-heap of K */
        /* for a probe this is fine */
        for(int i=0;i<Tr_n;i++){
            if(i<K)continue;
            /* if all[i] beats the K-th best, swap */
            /* find min in top K */
            int min_j=0;
            for(int j=1;j<K;j++)if(all[j].s<all[min_j].s)min_j=j;
            if(all[i].s>all[min_j].s){si tmp=all[min_j];all[min_j]=all[i];all[i]=tmp;}
        }
        /* now all[0..K-1] is the shortlist (not sorted) */
        si *sl=all; /* shortlist */

        /* was correct answer in shortlist? */
        int found=0;
        for(int j=0;j<K;j++)if(!strcmp(Tr[sl[j].i].label,Te[q].label)){found=1;break;}
        if(found)in_shortlist++;

        /* twin-only argmax: the best in shortlist by n-gram score */
        int twin_best=sl[0].i;
        for(int j=1;j<K;j++)if(sl[j].s>sl[0].s)sl[0]=sl[j]; /* reuse sl[0] as best */
        /* actually just find max score among ALL (not just shortlist) for twin-only */
        int twin_max_s=-1000000,twin_max_i=0;
        for(int i=0;i<Tr_n;i++){int s=t_score_pre(&qv,&T_idx[i],qa,T_act[i]);if(s>twin_max_s){twin_max_s=s;twin_max_i=i;}}
        twin_best=twin_max_i;
        if(!strcmp(Tr[twin_best].label,Te[q].label))twin_only_ok++;

        /* stage 2: rerank shortlist by word-set Dice */
        int cas_best=sl[0].i,cas_best_s=-1000000;
        for(int j=0;j<K;j++){
            int s=c_text_sim(Te[q].text,Tr[sl[j].i].text);
            if(s>cas_best_s){cas_best_s=s;cas_best=sl[j].i;}
        }
        /* tie-break: if word Dice ties, keep n-gram order */
        /* (the shortlist is already n-gram-sorted for the first pass) */

        if(!strcmp(Tr[cas_best].label,Te[q].label))cascade_ok++;

        if(cas_best!=twin_best){
            changed++;
            int cas_ok=!strcmp(Tr[cas_best].label,Te[q].label);
            int tw_ok=!strcmp(Tr[twin_best].label,Te[q].label);
            if(cas_ok&&!tw_ok)changed_correct++;
            if(!cas_ok&&tw_ok)changed_wrong++;
        }
        free(all);
    }

    printf("  twin-only:     %6.1f%%  (%d/%d)\n",100.0*twin_only_ok/Te_n,twin_only_ok,Te_n);
    printf("  cascade K=%d:  %6.1f%%  (%d/%d)\n",K,100.0*cascade_ok/Te_n,cascade_ok,Te_n);
    printf("  correct answer in shortlist: %6.1f%%  (%d/%d)\n",100.0*in_shortlist/Te_n,in_shortlist,Te_n);
    printf("  rerank changed %d: %d correct, %d wrong (net %+d)\n",changed,changed_correct,changed_wrong,changed_correct-changed_wrong);

    free(Tr);free(Te);free(T_idx);free(T_act);
    return 0;
}