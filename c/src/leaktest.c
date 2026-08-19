/* leaktest.c — do DEV utterances leak back into the index via NLU-Eval?
 * Dev is carved from MASSIVE train. MASSIVE derives FROM NLU-Eval. Dev items
 * are never hs_add'd, so the NLU dedup cannot see them. */
#include "router.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAXU 40000
static char *DEV[4000]; static int D_n;
static char *IDX[MAXU]; static int I_n;
static int js(const char*l,const char*k,char*o,int cap){
    char pat[64]; snprintf(pat,sizeof pat,"\"%s\":",k);
    const char*p=strstr(l,pat); if(!p)return 0; p+=strlen(pat);
    while(*p==' ')p++; if(*p!='"')return 0; p++;
    int n=0; while(*p&&*p!='"'&&n<cap-1){if(*p=='\\'&&p[1])p++;o[n++]=*p++;} o[n]=0; return 1; }
static int isiot(const char*l){return !strncmp(l,"iot_",4);}
int main(int argc,char**argv){
    char line[8192],t[512],l[RNAMELEN]; int ti=0;
    FILE*f=fopen(argv[1],"r");
    while(fgets(line,sizeof line,f)) if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l)){
        if(!isiot(l)) continue;
        if((ti++ % 4)==0) DEV[D_n++]=strdup(t);          /* dev, excluded from index */
    } fclose(f);
    /* the index also receives NLU-Eval iot rows */
    f=fopen(argv[2],"r");
    while(fgets(line,sizeof line,f)){
        char*fl[12]={0}; int nf=0,inq=0; char*p=line; fl[nf++]=p;
        for(;*p&&nf<12;p++){ if(*p=='"')inq=!inq; else if(*p==';'&&!inq){*p=0;fl[nf++]=p+1;} }
        if(nf<10)continue;
        for(int i=0;i<nf;i++){char*s=fl[i];int L=(int)strlen(s);
            while(L&&(s[L-1]=='\n'||s[L-1]=='\r'))s[--L]=0;
            if(L>=2&&s[0]=='"'&&s[L-1]=='"'){s[L-1]=0;fl[i]=s+1;} }
        if(strcmp(fl[2],"iot"))continue;
        IDX[I_n++]=strdup(fl[9]);
    } fclose(f);
    int leak=0;
    for(int i=0;i<D_n;i++){
        char a[512]; r_norm(DEV[i],a,sizeof a);
        for(int k=0;k<I_n;k++){ char b[512]; r_norm(IDX[k],b,sizeof b);
            if(!strcmp(a,b)){ leak++; break; } }
    }
    printf("\n  dev IoT utterances            : %d\n",D_n);
    printf("  NLU-Eval IoT rows in index    : %d\n",I_n);
    printf("  dev utterances found VERBATIM in the index via NLU-Eval : %d  (%.1f%%)\n",
           leak,100.0*leak/D_n);
    printf("\n  the NLU dedup checks hs_has(), but dev items are never hs_add'd,\n");
    printf("  so every dev utterance that also exists in NLU-Eval leaks straight back.\n");
    return 0;
}
