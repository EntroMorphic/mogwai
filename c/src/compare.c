/* compare.c — binary vs twin-ternary, same corpus, same protocol. Integer only. */
#include "ternary.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXU 40000
static char *U_t[MAXU]; static char U_l[MAXU][RNAMELEN]; static int U_n;
static char *V_t[3000]; static char V_l[3000][RNAMELEN]; static int V_n;
static char *T_t[4000]; static char T_l[4000][RNAMELEN]; static int T_n;
static router_t R; static tvec *TI; static tvec TSIG[RMAXCLS];

static int js(const char*l,const char*k,char*o,int cap){
    char pat[64]; snprintf(pat,sizeof pat,"\"%s\":",k);
    const char*p=strstr(l,pat); if(!p)return 0; p+=strlen(pat);
    while(*p==' ')p++; if(*p!='"')return 0; p++;
    int n=0; while(*p&&*p!='"'&&n<cap-1){if(*p=='\\'&&p[1])p++;o[n++]=*p++;} o[n]=0; return 1;
}
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

/* ---- the two routers ---- */
static int route_bin(const char *txt,int th){
    rvec q; r_encode(&R,txt,&q);
    int best=-RD-1; uint32_t bi=0;
    for(uint32_t i=0;i<R.n_index;i++){int s=r_sim(&q,&R.index[i]); if(s>best){best=s;bi=i;}}
    if(best<=th) return -1;
    int cls=r_apply_polarity(&R,R.label[bi],txt);
    int sb=-RD-1,sc=-1;
    for(uint32_t c=0;c<R.n_class;c++){int s=r_sim(&q,&R.sig[c]); if(s>sb){sb=s;sc=(int)c;}}
    return r_family(&R,sc)!=r_family(&R,cls) ? -1 : cls;
}
static int route_ter(const char *txt,int th){
    tvec q; t_encode(&R,txt,&q);
    int best=-1<<28; uint32_t bi=0;
    for(uint32_t i=0;i<R.n_index;i++){int s=t_score(&q,&TI[i]); if(s>best){best=s;bi=i;}}
    if(best<=th) return -1;
    int cls=r_apply_polarity(&R,R.label[bi],txt);
    int sb=-1<<28,sc=-1;
    for(uint32_t c=0;c<R.n_class;c++){int s=t_score(&q,&TSIG[c]); if(s>sb){sb=s;sc=(int)c;}}
    return r_family(&R,sc)!=r_family(&R,cls) ? -1 : cls;
}
typedef struct{int fa,wa,ms,iok,in;} TX;
static TX eval(int(*rt)(const char*,int),int th,char**ta,char la[][RNAMELEN],int n){
    TX z={0,0,0,0,0};
    for(int i=0;i<n;i++){
        int c=rt(ta[i],th);
        const char *p = c<0 ? "none" : R.names[c];
        int gn=!strcmp(la[i],"none");
        if(!gn){ z.in++; if(!strcmp(p,la[i])) z.iok++; }
        if(!strcmp(p,la[i])) continue;
        if(gn) z.fa++; else if(!strcmp(p,"none")) z.ms++; else z.wa++;
    }
    return z;
}
static int tune(int(*rt)(const char*,int),int lo,int hi,int step){
    int best=lo; long bc=1L<<60;
    for(int th=lo;th<=hi;th+=step){
        TX z=eval(rt,th,V_t,V_l,V_n);
        long c=3L*(z.fa+z.wa)+z.ms;
        if(c<bc){bc=c;best=th;}
    }
    return best;
}
int main(int argc,char**argv){
    if(argc<5){fprintf(stderr,"usage: compare train val test nlu.csv\n");return 1;}
    char line[8192],t[512],l[RNAMELEN];
    FILE*f=fopen(argv[1],"r");
    while(fgets(line,sizeof line,f)) if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l))
        { push(U_t,U_l,&U_n,t,isiot(l)?l:"none"); hs_add(t); }
    fclose(f); int n_train=U_n;
    f=fopen(argv[2],"r"); int vi=0;
    while(fgets(line,sizeof line,f)) if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l)){
        if(isiot(l)&&(vi++%2==0)){ push(U_t,U_l,&U_n,t,l); hs_add(t); }
        else push(V_t,V_l,&V_n,t,isiot(l)?l:"none"); }
    fclose(f);
    f=fopen(argv[3],"r");
    while(fgets(line,sizeof line,f)) if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l))
        { push(T_t,T_l,&T_n,t,isiot(l)?l:"none"); hs_add(t); }
    fclose(f);
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
    fprintf(stderr,"  index %d (train %d, +NLU %d new, %d dedup) | tune %d | test %d\n",
            U_n,n_train,added,dup,V_n,T_n);
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
    for(uint32_t c=0;c<R.n_class;c++){
        int cnt=0; static int ob[RD],om[RD]; memset(ob,0,sizeof ob); memset(om,0,sizeof om);
        for(int i=0;i<U_n;i++) if(R.label[i]==c){ cnt++;
            for(int d=0;d<RD;d++){ if(R.index[i].w[d>>5]&(1u<<(d&31))) ob[d]++;
                                   if(TI[i].m[d>>5]&(1u<<(d&31))) om[d]++; } }
        for(int d=0;d<RD;d++){
            if(cnt&&ob[d]*2>=cnt) R.sig[c].w[d>>5]|=1u<<(d&31);
            if(cnt&&om[d]*4>=cnt){ TSIG[c].m[d>>5]|=1u<<(d&31);
                if(ob[d]*2>=om[d]) TSIG[c].s[d>>5]|=1u<<(d&31); } } }
    int bt=tune(route_bin,-RD,RD,4), tt=tune(route_ter,-400,400,4);
    printf("  dev thresholds: binary %d, ternary %d\n\n",bt,tt);
    printf("  %-18s %-9s %-9s %-9s %s\n","representation","iot acc","wrong","missed","index KB");
    TX b=eval(route_bin,bt,T_t,T_l,T_n);
    printf("  %-18s %8.1f%% %-9d %-9d %.0f\n","binary (1 bit)",100.0*b.iok/b.in,b.fa+b.wa,b.ms,U_n*sizeof(rvec)/1024.0);
    TX x=eval(route_ter,tt,T_t,T_l,T_n);
    printf("  %-18s %8.1f%% %-9d %-9d %.0f\n","twin-ternary (2b)",100.0*x.iok/x.in,x.fa+x.wa,x.ms,U_n*sizeof(tvec)/1024.0);
    return 0;
}
