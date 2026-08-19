/* probe.c — diagnose the 10-point gap. Three questions:
 *   1. is t_score length-invariant? (asymmetric normalisation)
 *   2. how much does dimension buy in twin-ternary?
 *   3. does the MASK plane alone give a high-recall shortlist? (cascade)   */
#include "ternary.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAXU 40000
static char *U_t[MAXU]; static char U_l[MAXU][RNAMELEN]; static int U_n;
static char *T_t[4000]; static char T_l[4000][RNAMELEN]; static int T_n;
static router_t R; static tvec *TI;
static int js(const char*l,const char*k,char*o,int cap){
    char pat[64]; snprintf(pat,sizeof pat,"\"%s\":",k);
    const char*p=strstr(l,pat); if(!p)return 0; p+=strlen(pat);
    while(*p==' ')p++; if(*p!='"')return 0; p++;
    int n=0; while(*p&&*p!='"'&&n<cap-1){if(*p=='\\'&&p[1])p++;o[n++]=*p++;} o[n]=0; return 1; }
static int isiot(const char*l){return !strncmp(l,"iot_",4);}
/* Dice: symmetric, integer, length-invariant */
static int dice(const tvec*a,const tvec*b,int aa){
    int d=t_dot(a,b), ab=t_active(b);
    return (2*d*256)/(aa+ab+8);
}
int main(int argc,char**argv){
    char line[8192],t[512],l[RNAMELEN];
    FILE*f=fopen(argv[1],"r");
    while(fgets(line,sizeof line,f)) if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l))
        { U_t[U_n]=strdup(t); snprintf(U_l[U_n],RNAMELEN,"%s",isiot(l)?l:"none"); U_n++; }
    fclose(f);
    f=fopen(argv[2],"r");
    while(fgets(line,sizeof line,f)) if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l))
        { T_t[T_n]=strdup(t); snprintf(T_l[T_n],RNAMELEN,"%s",isiot(l)?l:"none"); T_n++; }
    fclose(f);
    memset(&R,0,sizeof R); R.dim=RD; R.n_index=U_n;
    int64_t sum[RD]; memset(sum,0,sizeof sum); int16_t acc[RD]; int32_t tot;
    for(int i=0;i<U_n;i++){ r_counts(U_t[i],acc,&tot);
        for(int d=0;d<RD;d++) sum[d]+=((int64_t)acc[d]*RSCALE)/tot; }
    for(int d=0;d<RD;d++) R.centre[d]=(int32_t)(sum[d]/U_n);
    TI=calloc(U_n,sizeof(tvec));
    for(int i=0;i<U_n;i++) t_encode(&R,U_t[i],&TI[i]);

    printf("  1. LENGTH INVARIANCE — mean best-score by query word count\n");
    printf("     %-8s %-8s %-12s %s\n","words","n","t_score","dice");
    for(int lo=2,hi=4; lo<=12; lo+=3,hi+=3){
        long s1=0,s2=0; int n=0;
        for(int i=0;i<T_n;i++){
            int w=1; for(const char*p=T_t[i];*p;p++) if(*p==' ')w++;
            if(w<lo||w>hi) continue;
            tvec q; t_encode(&R,T_t[i],&q); int aa=t_active(&q);
            int b1=-1<<28,b2=-1<<28;
            for(int k=0;k<U_n;k++){ int a=t_score(&q,&TI[k]); if(a>b1)b1=a;
                                     int b=dice(&q,&TI[k],aa); if(b>b2)b2=b; }
            s1+=b1; s2+=b2; n++;
            if(n>=120) break;
        }
        if(n) printf("     %d-%-6d %-8d %-12ld %ld\n",lo,hi,n,s1/n,s2/n);
    }
    printf("\n  2. MASK-PLANE-ONLY recall (cascade stage 1, half the ops)\n");
    int iotn=0, hit1=0, hit20=0, hit100=0;
    for(int i=0;i<T_n && iotn<220;i++){
        if(!strcmp(T_l[i],"none")) continue;
        iotn++;
        tvec q; t_encode(&R,T_t[i],&q);
        static int sc[MAXU];
        for(int k=0;k<U_n;k++){
            int ov=0;
            for(int wI=0;wI<RWORDS;wI++) ov+=__builtin_popcount(q.m[wI]&TI[k].m[wI]);
            sc[k]=ov;
        }
        /* top-K by mask overlap: does the gold class appear? */
        for(int K=1;K<=100;K*=10){
            int thr=0; /* select K-th largest crudely */
            int cnt[513]; memset(cnt,0,sizeof cnt);
            for(int k=0;k<U_n;k++) cnt[sc[k]>512?512:sc[k]]++;
            int acc2=0; for(thr=512; thr>=0; thr--){ acc2+=cnt[thr]; if(acc2>=K) break; }
            int found=0;
            for(int k=0;k<U_n && !found;k++) if(sc[k]>=thr && !strcmp(U_l[k],T_l[i])) found=1;
            if(K==1) hit1+=found; else if(K==10) hit20+=found; else hit100+=found;
        }
    }
    printf("     over %d iot queries: recall@1 %.1f%%  @10 %.1f%%  @100 %.1f%%\n",
           iotn,100.0*hit1/iotn,100.0*hit20/iotn,100.0*hit100/iotn);
    return 0;
}
