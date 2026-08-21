/* mkblob.c — emit the twin-ternary router as a flat blob for the ESP32,
 * plus a reference set (query, host score, host class) so the device can
 * prove PARITY rather than merely look plausible. Integer only. */
#include "ternary.h"
#include "prune.h"
#include "invariants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAXU 40000
#define NREF 64
static char *U_t[MAXU]; static char U_l[MAXU][RNAMELEN]; static int U_n;
static char *V_t[3000]; static char V_l[3000][RNAMELEN]; static int V_n;
static char *T_t[4000]; static int T_n;
static router_t R; static tvec *TI;
static prune_opt PRUNE = {0,0,0};
static int THRESH = -1;   /* --threshold=N; required when pruning */
static int js(const char*l,const char*k,char*o,int cap){
    char pat[64]; snprintf(pat,sizeof pat,"\"%s\":",k);
    const char*p=strstr(l,pat); if(!p)return 0; p+=strlen(pat);
    while(*p==' ')p++; if(*p!='"')return 0; p++;
    int n=0; while(*p&&*p!='"'&&n<cap-1){if(*p=='\\'&&p[1])p++;o[n++]=*p++;} o[n]=0; return 1; }
static int isiot(const char*l){return !strncmp(l,"iot_",4);}
#define HN 65536
static char *HS[HN];
static void hs_add(const char*s){ char b[512]; r_norm(s,b,sizeof b);
    uint32_t h=r_fnv(b,(int)strlen(b))%HN;
    while(HS[h]){ if(!strcmp(HS[h],b))return; h=(h+1)%HN; } HS[h]=strdup(b); }
static int hs_has(const char*s){ char b[512]; r_norm(s,b,sizeof b);
    uint32_t h=r_fnv(b,(int)strlen(b))%HN;
    while(HS[h]){ if(!strcmp(HS[h],b))return 1; h=(h+1)%HN; } return 0; }
static void push(char**ta,char la[][RNAMELEN],int*n,const char*t,const char*l){
    ta[*n]=strdup(t); if(la) snprintf(la[*n],RNAMELEN,"%s",l); (*n)++; }

int main(int argc,char**argv){
    if(argc<6){fprintf(stderr,"usage: mkblob train val test nlu.csv out.bin [--prune-dup] [--prune-cnn] [--prune-neg=K]\n");return 1;}
    for(int i=6;i<argc;i++) if(!strncmp(argv[i],"--threshold=",12)) THRESH=atoi(argv[i]+12);
        else if(!prune_parse(argv[i],&PRUNE))
        { fprintf(stderr,"  unknown flag %s\n",argv[i]); return 1; }
    char line[8192],t[512],l[RNAMELEN]; FILE*f;
    f=fopen(argv[3],"r");
    while(fgets(line,sizeof line,f)) if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l))
        { push(T_t,NULL,&T_n,t,NULL); hs_add(t); }
    fclose(f);
    f=fopen(argv[1],"r"); int ti=0,tn=0;
    while(fgets(line,sizeof line,f)) if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l)){
        int io=isiot(l);
        if(hs_has(t)) continue;
        if(io&&(ti++%4)==0){ push(V_t,V_l,&V_n,t,l); hs_add(t); continue; }
        if(!io&&(tn++%8)==0){ push(V_t,V_l,&V_n,t,"none"); hs_add(t); continue; }
        push(U_t,U_l,&U_n,t,io?l:"none"); hs_add(t);
    }
    fclose(f);
    f=fopen(argv[2],"r");
    while(fgets(line,sizeof line,f)) if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l))
        if(isiot(l)&&!hs_has(t)){ push(U_t,U_l,&U_n,t,l); hs_add(t); }
    fclose(f);
    f=fopen(argv[4],"r"); int added=0;
    while(fgets(line,sizeof line,f)){
        char*fl[12]={0}; int nf=0,inq=0; char*p=line; fl[nf++]=p;
        for(;*p&&nf<12;p++){ if(*p=='"')inq=!inq; else if(*p==';'&&!inq){*p=0;fl[nf++]=p+1;} }
        if(nf<10)continue;
        for(int i=0;i<nf;i++){char*s=fl[i];int L=(int)strlen(s);
            while(L&&(s[L-1]=='\n'||s[L-1]=='\r'))s[--L]=0;
            if(L>=2&&s[0]=='"'&&s[L-1]=='"'){s[L-1]=0;fl[i]=s+1;} }
        if(strcmp(fl[2],"iot")||hs_has(fl[9]))continue;
        char lb[RNAMELEN]; snprintf(lb,sizeof lb,"iot_%s",fl[3]);
        push(U_t,U_l,&U_n,fl[9],lb); hs_add(fl[9]); added++;
    }
    fclose(f);
    inv_disjoint("index vs DEV",  U_t,U_n,V_t,V_n);
    inv_disjoint("index vs TEST", U_t,U_n,T_t,T_n);
    memset(&R,0,sizeof R); R.magic=RMAGIC; R.dim=RD; R.n_index=U_n;
    for(int i=0;i<U_n;i++){ int fnd=-1;
        for(uint32_t c=0;c<R.n_class;c++) if(!strcmp(R.names[c],U_l[i])){fnd=c;break;}
        if(fnd<0&&R.n_class<RMAXCLS) snprintf(R.names[R.n_class++],RNAMELEN,"%s",U_l[i]); }
    int64_t sum[RD]; memset(sum,0,sizeof sum); int16_t acc[RD]; int32_t tot;
    for(int i=0;i<U_n;i++){ r_counts(U_t[i],acc,&tot);
        for(int d=0;d<RD;d++) sum[d]+=((int64_t)acc[d]*RSCALE)/tot; }
    for(int d=0;d<RD;d++) R.centre[d]=(int32_t)(sum[d]/U_n);
    R.label=calloc(U_n,1); TI=calloc(U_n,sizeof(tvec));
    for(int i=0;i<U_n;i++){ t_encode(&R,U_t[i],&TI[i]);
        for(uint32_t c=0;c<R.n_class;c++) if(!strcmp(R.names[c],U_l[i])){R.label[i]=c;break;} }
    /* same prune the harness measured — one implementation, see prune.h */
    U_n = prune_index(U_t,U_l,&R,TI,NULL,U_n,PRUNE,1);
    /* The threshold is tuned on dev by `compare`, not here. It is NOT invariant
       under pruning — baseline and --prune-cnn both tune to 136, but cnn+2 tunes
       to 146 and --prune-neg=8 to 154. Shipping 136 with a pruned index puts the
       device at an operating point the harness never measured, and PARITY still
       passes because the host reference uses the same wrong value. Parity proves
       host==device, not that either is right. So: refuse to guess. */
    if(THRESH>=0) R.threshold=THRESH;
    else if(PRUNE.dup||PRUNE.cnn||PRUNE.neg_k>1){
        fprintf(stderr,"  ERROR: pruning changes the tuned threshold. Run\n"
                       "    ./c/bin/compare <data> <prune flags>\n"
                       "  and pass the reported th=N as --threshold=N\n"); return 1; }
    else R.threshold=RSHIP_TH;   /* single source of truth, see router.h */
    /* host reference: first NREF dev queries, with the answer this host gives */
    FILE*o=fopen(argv[5],"wb");
    uint32_t hdr[5]={RMAGIC,RD,(uint32_t)U_n,R.n_class,(uint32_t)R.threshold};
    fwrite(hdr,4,5,o);
    fwrite(R.names,RNAMELEN,RMAXCLS,o);
    fwrite(R.centre,sizeof(int32_t),RD,o);
    fwrite(R.label,1,U_n,o);
    { uint16_t *act=malloc(U_n*2);
      for(int i=0;i<U_n;i++) act[i]=(uint16_t)t_active(&TI[i]);
      fwrite(act,2,U_n,o); free(act); }
    fwrite(TI,sizeof(tvec),U_n,o);
    uint32_t nref=(V_n<NREF)?V_n:NREF; fwrite(&nref,4,1,o);
    for(uint32_t i=0;i<nref;i++){
        tvec q; t_encode(&R,V_t[i],&q); int aa=t_active(&q);
        int best=-(1<<28); uint32_t bi=0;
        for(int k=0;k<U_n;k++){int s=t_score(&q,&TI[k],aa); if(s>best){best=s;bi=k;}}
        int cls = (best>R.threshold) ? r_apply_polarity(&R,R.label[bi],V_t[i]) : -1;
        uint8_t len=(uint8_t)strlen(V_t[i]);
        fwrite(&len,1,1,o); fwrite(V_t[i],1,len,o);
        int32_t sc=best; int8_t c8=(int8_t)cls;
        fwrite(&sc,4,1,o); fwrite(&c8,1,1,o);
    }
    long bytes=ftell(o); fclose(o);
    fprintf(stderr,"  index %d (+NLU %d)  classes %u  refs %u\n",U_n,added,R.n_class,nref);
    fprintf(stderr,"  wrote %s: %ld bytes (%.0f KB)\n",argv[5],bytes,bytes/1024.0);
    return 0;
}
