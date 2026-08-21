/* leakchk.c — is the dev split optimistically biased? For each dev IoT query,
 * how similar is its nearest INDEX entry, by exact word overlap? Near-duplicates
 * mean dev measures memorisation, not generalisation. */
#include "cascade.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAXU 40000
static char *U_t[MAXU]; static int U_n; static char U_l[MAXU][RNAMELEN];
static char *D_t[3000]; static int D_n;
static int js(const char*l,const char*k,char*o,int cap){
    char pat[64]; snprintf(pat,sizeof pat,"\"%s\":",k);
    const char*p=strstr(l,pat); if(!p)return 0; p+=strlen(pat);
    while(*p==' ')p++; if(*p!='"')return 0; p++;
    int n=0; while(*p&&*p!='"'&&n<cap-1){if(*p=='\\'&&p[1])p++;o[n++]=*p++;} o[n]=0; return 1; }
static int isiot(const char*l){return !strncmp(l,"iot_",4);}
static void hist(const char *name,char **Q,int qn){
    int b[6]={0,0,0,0,0,0};
    for(int i=0;i<qn;i++){
        int best=0;
        for(int k=0;k<U_n;k++){ int s=c_text_sim(Q[i],U_t[k]); if(s>best) best=s; }
        int pct=best*100/256;
        if(pct>=95)b[0]++; else if(pct>=80)b[1]++; else if(pct>=60)b[2]++;
        else if(pct>=40)b[3]++; else if(pct>=20)b[4]++; else b[5]++;
    }
    printf("  %-10s n=%-5d  >=95%%:%-4d 80-95:%-4d 60-80:%-4d 40-60:%-4d 20-40:%-4d <20:%-4d\n",
           name,qn,b[0],b[1],b[2],b[3],b[4],b[5]);
    printf("  %-10s near-duplicate rate (>=80%% word overlap): %.1f%%\n\n",
           "",100.0*(b[0]+b[1])/qn);
}
int main(int argc,char**argv){
    if(argc<4){ fprintf(stderr,
        "usage: leakchk <train.json> <validation.json> <test.json>\n"
        "  Is the DEV split optimistically biased? Compares dev-vs-index\n"
        "  similarity against test-vs-index.\n"
        "  NOTE: reads test.json. It measures a similarity DISTRIBUTION, not\n"
        "  accuracy, so it does not consume TEST_BUDGET - but do not extend it\n"
        "  into anything that reports a test score.\n"); return 1; }
    char line[8192],t[512],l[RNAMELEN];
    FILE*f=fopen(argv[1],"r"); int ti=0;
    while(fgets(line,sizeof line,f)) if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l)){
        if(!isiot(l)) continue;
        if((ti++ % 4)==0){ D_t[D_n++]=strdup(t); continue; }
        U_t[U_n]=strdup(t); snprintf(U_l[U_n],RNAMELEN,"%s",l); U_n++;
    }
    fclose(f);
    f=fopen(argv[2],"r");
    while(fgets(line,sizeof line,f)) if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l))
        if(isiot(l)){ U_t[U_n]=strdup(t); snprintf(U_l[U_n],RNAMELEN,"%s",l); U_n++; }
    fclose(f);
    static char *TE[3000]; int te=0;
    f=fopen(argv[3],"r");
    while(fgets(line,sizeof line,f)) if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l))
        if(isiot(l)) TE[te++]=strdup(t);
    fclose(f);
    printf("\n  nearest INDEX neighbour by word-overlap Dice (index = %d IoT)\n\n",U_n);
    hist("DEV",D_t,D_n);
    hist("TEST",TE,te);
    return 0;
}
