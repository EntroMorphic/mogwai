/* errdump.c — dump every 60-class error: what was said, what was predicted,
 * what was the nearest neighbour, and the score margin. Then a confusion
 * matrix and a per-class breakdown, so the gap to 100% has a shape.
 *
 * No float on the hot path. int32_t is the widest type. No Python. */
#include "router.h"
#include "ternary.h"
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
    char pat[32];int pl=snprintf(pat,sizeof pat,"\"%s\":",key);
    const char *p=strstr(line,pat);if(!p)return 0;p+=pl;
    while(*p==' '||*p=='\t')p++;
    if(*p!='"')return 0;p++;
    int i=0;while(*p&&*p!='"'&&i<sz-1){if(*p=='\\'&&p[1])p++;out[i++]=*p++;}out[i]=0;return 1;
}

int main(int argc,char**argv){
    if(argc<3){fprintf(stderr,"usage: errdump <train.json> <test.json> [--confusion]\n");return 1;}
    int do_conf=argc>3&&strstr(argv[3],"conf");
    FILE*f=fopen(argv[1],"r");char line[8192],t[512],l[RNAMELEN];
    while(fgets(line,sizeof line,f))if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l))push(&Tr,&Tr_n,&Tr_cap,t,l);
    fclose(f);f=fopen(argv[2],"r");
    while(fgets(line,sizeof line,f))if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l))push(&Te,&Te_n,&Te_cap,t,l);
    fclose(f);

    char cls[MC_MAXCLS][RNAMELEN];int ncls=0;
    int cls_tr[MC_MAXCLS]={0}; /* train count per class */
    for(int i=0;i<Tr_n;i++){
        int fd=-1;for(int c=0;c<ncls;c++)if(!strcmp(cls[c],Tr[i].label)){fd=c;break;}
        if(fd<0&&ncls<MC_MAXCLS){strncpy(cls[ncls],Tr[i].label,RNAMELEN);fd=ncls++;}
        cls_tr[fd]++;
    }

    router_t r;memset(&r,0,sizeof r);r.dim=RD;
    {int32_t *acc=calloc(RD,sizeof(int32_t));int64_t total=0;int16_t c[RD];
     for(int i=0;i<Tr_n;i++){int32_t tt;r_counts(Tr[i].text,c,&tt);total+=tt;for(int d=0;d<RD;d++)acc[d]+=c[d];}
     for(int d=0;d<RD;d++)r.centre[d]=(int32_t)(total>0?(acc[d]*(int64_t)RSCALE)/total:0);
     free(acc);}

    t_popcnt_init();
    tvec *T_idx=malloc(Tr_n*sizeof(tvec));int *T_act=malloc(Tr_n*sizeof(int));
    for(int i=0;i<Tr_n;i++){t_encode(&r,Tr[i].text,&T_idx[i]);T_act[i]=t_active(&T_idx[i]);}

    /* confusion matrix */
    int conf[MC_MAXCLS][MC_MAXCLS];memset(conf,0,sizeof conf);
    int te_per_cls[MC_MAXCLS]={0};
    int correct=0;

    printf("=== ERRORS (twin-ternary d=%d) ===\n",RD);
    for(int q=0;q<Te_n;q++){
        tvec qv;t_encode(&r,Te[q].text,&qv);int qa=t_active(&qv);
        int best_s=-1000000,best_i=0,second_s=-1000000;
        for(int i=0;i<Tr_n;i++){
            int s=t_score_pre(&qv,&T_idx[i],qa,T_act[i]);
            if(s>best_s){second_s=best_s;best_s=s;best_i=i;}
            else if(s>second_s){second_s=s;}
        }
        int tc=-1,pred_c=-1;
        for(int c=0;c<ncls;c++){if(!strcmp(cls[c],Te[q].label))tc=c;if(!strcmp(cls[c],Tr[best_i].label))pred_c=c;}
        if(tc>=0){te_per_cls[tc]++;conf[tc][pred_c]++;}
        if(!strcmp(Tr[best_i].label,Te[q].label)){correct++;continue;}
        printf("  [%-22s] -> [%-22s]  score %4d  margin %+4d  | test: \"%s\"  | nn: \"%s\"\n",
               Te[q].label, Tr[best_i].label, best_s, best_s-second_s,
               Te[q].text, Tr[best_i].text);
    }

    printf("\n=== SUMMARY ===\n");
    printf("  total %d, correct %d (%.1f%%), wrong %d\n",Te_n,correct,100.0*correct/Te_n,Te_n-correct);

    /* per-class accuracy, sorted by error count */
    printf("\n=== PER-CLASS (sorted by errors) ===\n");
    printf("  %-22s %5s %5s %5s %5s\n","class","test","train","errs","acc%");
    typedef struct{int idx,errs,te,tr;} ce;
    ce sorted[MC_MAXCLS];int ns=0;
    for(int c=0;c<ncls;c++){
        if(te_per_cls[c]==0)continue;
        int e=te_per_cls[c]-conf[c][c];
        sorted[ns].idx=c;sorted[ns].errs=e;sorted[ns].te=te_per_cls[c];sorted[ns].tr=cls_tr[c];ns++;
    }
    for(int i=0;i<ns;i++)for(int j=i+1;j<ns;j++)if(sorted[j].errs>sorted[i].errs){ce t=sorted[i];sorted[i]=sorted[j];sorted[j]=t;}
    for(int i=0;i<ns;i++){
        int c=sorted[i].idx;
        printf("  %-22s %5d %5d %5d  %4.1f\n",cls[c],te_per_cls[c],cls_tr[c],sorted[i].errs,
               100.0*conf[c][c]/te_per_cls[c]);
    }

    if(do_conf){
        printf("\n=== CONFUSION (actual > predicted, errors only) ===\n");
        for(int a=0;a<ncls;a++)for(int b=0;b<ncls;b++)
            if(a!=b&&conf[a][b]>0)
                printf("  %-22s -> %-22s  %4d\n",cls[a],cls[b],conf[a][b]);
    }

    /* error categories: same-scenario vs cross-scenario */
    /* MASSIVE labels are <scenario>_<intent>; group by scenario prefix */
    printf("\n=== ERROR STRUCTURE ===\n");
    int same_scene=0,cross_scene=0;
    for(int q=0;q<Te_n;q++){
        tvec qv;t_encode(&r,Te[q].text,&qv);int qa=t_active(&qv);
        int best_s=-1000000,best_i=0;
        for(int i=0;i<Tr_n;i++){int s=t_score_pre(&qv,&T_idx[i],qa,T_act[i]);if(s>best_s){best_s=s;best_i=i;}}
        if(!strcmp(Tr[best_i].label,Te[q].label))continue;
        /* extract scenario: everything before the first _ */
        char sa[32],sb[32];strncpy(sa,Te[q].label,31);sa[31]=0;char*u=strchr(sa,'_');if(u)*u=0;
        strncpy(sb,Tr[best_i].label,31);sb[31]=0;char*v=strchr(sb,'_');if(v)*v=0;
        if(!strcmp(sa,sb))same_scene++;else cross_scene++;
    }
    printf("  same-scenario (e.g. calendar_set -> calendar_query): %d\n",same_scene);
    printf("  cross-scenario (e.g. calendar_set -> email_query):    %d\n",cross_scene);

    free(Tr);free(Te);free(T_idx);free(T_act);
    return 0;
}