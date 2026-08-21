/* prdiag.c — what does the word prior actually say? */
#include "prior.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAXU 40000
static char *U_t[MAXU]; static uint8_t U_y[MAXU]; static int U_n;
static char  NAMES[RMAXCLS][RNAMELEN]; static int NC;
static char *D_t[3000]; static char D_l[3000][RNAMELEN]; static int D_n;
static int js(const char*l,const char*k,char*o,int cap){
    char pat[64]; snprintf(pat,sizeof pat,"\"%s\":",k);
    const char*p=strstr(l,pat); if(!p)return 0; p+=strlen(pat);
    while(*p==' ')p++; if(*p!='"')return 0; p++;
    int n=0; while(*p&&*p!='"'&&n<cap-1){if(*p=='\\'&&p[1])p++;o[n++]=*p++;} o[n]=0; return 1; }
static int isiot(const char*l){return !strncmp(l,"iot_",4);}
static int cls_of(const char*l){
    for(int i=0;i<NC;i++) if(!strcmp(NAMES[i],l)) return i;
    snprintf(NAMES[NC],RNAMELEN,"%s",l); return NC++; }
int main(int argc,char**argv){
    if(argc<2){ fprintf(stderr,
        "usage: prdiag <train.json>\n"
        "  What does the word-lift prior actually say? The prior was measured\n"
        "  inert and cut from the router; this is why.\n"); return 1; }
    char line[8192],t[512],l[RNAMELEN]; int ti=0;
    FILE*f=fopen(argv[1],"r");
    while(fgets(line,sizeof line,f)) if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l)){
        int io=isiot(l);
        const char *lab = io ? l : "none";
        if(io && (ti++ % 4)==0){ D_t[D_n]=strdup(t); snprintf(D_l[D_n],RNAMELEN,"%s",lab); D_n++; continue; }
        U_t[U_n]=strdup(t); U_y[U_n]=(uint8_t)cls_of(lab); U_n++;
    }
    fclose(f);
    static prior_t P; pr_build(&P,U_t,U_y,U_n,NC);
    printf("\n  index %d utterances, %d classes\n",U_n,NC);
    int none_id=cls_of("none");
    int votes[RMAXCLS]; memset(votes,0,sizeof votes);
    int strong=0, correct=0;
    for(int i=0;i<D_n;i++){
        int mg=0, pc=pr_vote(&P,D_t[i],&mg);
        if(pc>=0) votes[pc]++;
        if(mg>=4*PR_SCALE) strong++;
        if(pc>=0 && !strcmp(NAMES[pc],D_l[i])) correct++;
    }
    printf("\n  prior's vote on %d DEV IoT queries:\n",D_n);
    for(int c=0;c<NC;c++) if(votes[c])
        printf("    %-22s %4d  (%.0f%%)%s\n",NAMES[c],votes[c],100.0*votes[c]/D_n,
               c==none_id?"   <-- the negative class":"");
    printf("\n  prior alone is correct on %d/%d (%.1f%%)\n",correct,D_n,100.0*correct/D_n);
    printf("  strong-disagreement branch (mg >= 4*PR_SCALE) fired %d times (%.0f%%)\n",
           strong,100.0*strong/D_n);
    return 0;
}
