/* multiclass.c — 60-class MASSIVE: does the representation claim generalise
 * beyond the 9-class IoT routing problem?
 *
 * The shipped router treats 9 iot_* intents as commands and everything else
 * as negatives. That is a routing problem: accept or reject. This tool asks
 * a different question — can the representation distinguish ALL 60 MASSIVE
 * intents by nearest neighbour? — on the same corpus, same split, same
 * encodings, same size-matched control.
 *
 * The structural argument for twin-ternary is that binary forces "no
 * evidence" to -1, so sparse vectors agree on hundreds of dims that carry
 * no information. On 9 classes that costs binary ~6 recall points. On 60
 * classes the distinctions are finer and the sparsity is higher (most
 * dims are 0 for any given intent), so the argument predicts the gap
 * WIDENS. This is the pre-registered prediction.
 *
 * No float on the hot path. int32_t is the widest type. No Python.
 */
#include "router.h"
#include "ternary.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MC_MAXCLS 128

typedef struct { char text[512]; char label[RNAMELEN]; } ex;
static ex *Tr=0, *Te=0; static int Tr_n, Tr_cap, Te_n, Te_cap;

static void push(ex **a, int *n, int *c, const char *t, const char *l) {
    if (*n==*c){ *c=*c?*c*2:4096; *a=realloc(*a,*c*sizeof(ex)); }
    strncpy((*a)[*n].text,t,sizeof((*a)[*n].text)-1); (*a)[*n].text[sizeof((*a)[*n].text)-1]=0;
    strncpy((*a)[*n].label,l,RNAMELEN-1); (*a)[*n].label[RNAMELEN-1]=0; (*n)++;
}

static int js(const char *line, const char *key, char *out, int sz) {
    char pat[32]; int pl=snprintf(pat,sizeof pat,"\"%s\":",key);
    const char *p=strstr(line,pat); if(!p) return 0; p+=pl;
    while(*p==' '||*p=='\t') p++;
    if(*p != '"') return 0;
    p++;
    int i=0; while(*p&&*p!='"'&&i<sz-1){ if(*p=='\\'&&p[1])p++; out[i++]=*p++; }
    out[i]=0; return 1;
}

/* binary encoding at a given dimension count */
static void b_encode(const int32_t *centre, const char *text, uint32_t *out, int rd) {
    int16_t acc[256]; int32_t total=0;
    r_counts(text, acc, &total);
    int w=rd/32; for(int i=0;i<w;i++) out[i]=0;
    for(int i=0;i<rd;i++){
        int bit=(acc[i]*RSCALE > centre[i])?1:0;
        if(bit) out[i/32]|=(1u<<(i%32));
    }
}
static int b_score(const uint32_t *q, const uint32_t *b, int rd) {
    int w=rd/32, dot=0;
    for(int i=0;i<w;i++){ unsigned x=q[i]^b[i]; dot+=32-2*__builtin_popcount(x); }
    return (2*dot*256)/(rd+rd);
}

static void usage(void){
    fprintf(stderr,
        "usage: multiclass <train.json> <test.json>\n"
        "\n"
        "  60-class nearest-neighbour accuracy on MASSIVE: twin-ternary\n"
        "  (2 bit/dim, d=256, 64 B/vec) vs binary (1 bit/dim, d=256 and\n"
        "  d=512 size-matched), paired per-item. Reports ROW lines for\n"
        "  machine-readable logging and a human summary.\n");
}

int main(int argc, char **argv){
    if(argc!=3){ usage(); return 1; }

    /* load */
    FILE *f=fopen(argv[1],"r"); if(!f){ fprintf(stderr,"cannot open %s\n",argv[1]); return 1; }
    char line[8192],t[512],l[RNAMELEN];
    while(fgets(line,sizeof line,f)) if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l))
        push(&Tr,&Tr_n,&Tr_cap,t,l);
    fclose(f);
    f=fopen(argv[2],"r"); if(!f){ fprintf(stderr,"cannot open %s\n",argv[2]); return 1; }
    while(fgets(line,sizeof line,f)) if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l))
        push(&Te,&Te_n,&Te_cap,t,l);
    fclose(f);

    /* class inventory */
    char cls[MC_MAXCLS][RNAMELEN]; int ncls=0;
    for(int i=0;i<Tr_n;i++){ int fd=-1;
        for(int c=0;c<ncls;c++) if(!strcmp(cls[c],Tr[i].label)){fd=c;break;}
        if(fd<0&&ncls<MC_MAXCLS){ strncpy(cls[ncls],Tr[i].label,RNAMELEN); ncls++; } }
    fprintf(stderr,"  MASSIVE: train %d, test %d, classes %d\n",Tr_n,Te_n,ncls);

    /* build centre from train */
    router_t r; memset(&r,0,sizeof r); r.dim=RD;
    { int32_t *acc=calloc(RD,sizeof(int32_t)); int64_t total=0; int16_t c[RD];
      for(int i=0;i<Tr_n;i++){ int32_t tt; r_counts(Tr[i].text,c,&tt); total+=tt;
          for(int d=0;d<RD;d++) acc[d]+=c[d]; }
      for(int d=0;d<RD;d++) r.centre[d]=(int32_t)(total>0?(acc[d]*(int64_t)RSCALE)/total:0);
      free(acc); }

    /* 512-dim centre for binary d=512 */
    int32_t centre512[512]; memset(centre512,0,sizeof centre512);
    { int32_t *acc=calloc(512,sizeof(int32_t)); int64_t total=0; int16_t c[256];
      for(int i=0;i<Tr_n;i++){ int32_t tt; r_counts(Tr[i].text,c,&tt); total+=tt;
          for(int d=0;d<256;d++) acc[d]+=c[d];
          char salted[600]; snprintf(salted,sizeof salted,"~%s",Tr[i].text);
          int16_t c2[256]; int32_t t2; r_counts(salted,c2,&t2); total+=t2;
          for(int d=0;d<256;d++) acc[256+d]+=c2[d]; }
      for(int d=0;d<512;d++) centre512[d]=(int32_t)(total>0?(acc[d]*(int64_t)RSCALE)/total:0);
      free(acc); }

    /* encode index */
    t_popcnt_init();
    int w512=512/32;
    tvec *T_idx=malloc(Tr_n*sizeof(tvec));
    int *T_act=malloc(Tr_n*sizeof(int));
    uint32_t *B256=malloc(Tr_n*(256/32)*sizeof(uint32_t));
    uint32_t *B512=malloc(Tr_n*w512*sizeof(uint32_t));
    for(int i=0;i<Tr_n;i++){
        t_encode(&r,Tr[i].text,&T_idx[i]); T_act[i]=t_active(&T_idx[i]);
        b_encode(r.centre,Tr[i].text,&B256[i*(256/32)],256);
        b_encode(centre512,Tr[i].text,&B512[i*w512],512);
    }

    /* classify */
    int twin_ok=0,b256_ok=0,b512_ok=0;
    int *tp=malloc(Te_n*sizeof(int)),*bp256=malloc(Te_n*sizeof(int)),*bp512=malloc(Te_n*sizeof(int));
    for(int q=0;q<Te_n;q++){
        tvec qv; t_encode(&r,Te[q].text,&qv); int qa=t_active(&qv);
        int bs=-1000000,bi=0;
        for(int i=0;i<Tr_n;i++){ int s=t_score_pre(&qv,&T_idx[i],qa,T_act[i]); if(s>bs){bs=s;bi=i;} }
        tp[q]=bi; if(!strcmp(Tr[bi].label,Te[q].label)) twin_ok++;

        uint32_t qv256[256/32]; b_encode(r.centre,Te[q].text,qv256,256);
        bs=-1000000; bi=0;
        for(int i=0;i<Tr_n;i++){ int s=b_score(qv256,&B256[i*(256/32)],256); if(s>bs){bs=s;bi=i;} }
        bp256[q]=bi; if(!strcmp(Tr[bi].label,Te[q].label)) b256_ok++;

        uint32_t qv512[16]; b_encode(centre512,Te[q].text,qv512,512);
        bs=-1000000; bi=0;
        for(int i=0;i<Tr_n;i++){ int s=b_score(qv512,&B512[i*w512],512); if(s>bs){bs=s;bi=i;} }
        bp512[q]=bi; if(!strcmp(Tr[bi].label,Te[q].label)) b512_ok++;
    }

    /* paired */
    int tf256=0,bf256=0,tf512=0,bf512=0;
    for(int q=0;q<Te_n;q++){
        int tw=!strcmp(Tr[tp[q]].label,Te[q].label);
        int b2=!strcmp(Tr[bp256[q]].label,Te[q].label);
        int b5=!strcmp(Tr[bp512[q]].label,Te[q].label);
        if(tw&&!b2) tf256++;
        if(b2&&!tw) bf256++;
        if(tw&&!b5) tf512++;
        if(b5&&!tw) bf512++;
    }
    /* McNemar exact two-sided */
    double p256=1.0,p512=1.0;
    { int n=tf256+bf256; double s=0,tot=0;
      for(int k=0;k<=n;k++){ double w=1; for(int j=0;j<k;j++) w=w*(n-j)/(j+1); tot+=w;
          int lo=n<2*tf256?tf256:n-tf256, hi=n<2*tf256?n-tf256:tf256;
          if(k>=lo&&k<=hi) s+=w; }
      p256=n?s/tot:1.0; }
    { int n=tf512+bf512; double s=0,tot=0;
      for(int k=0;k<=n;k++){ double w=1; for(int j=0;j<k;j++) w=w*(n-j)/(j+1); tot+=w;
          int lo=n<2*tf512?tf512:n-tf512, hi=n<2*tf512?n-tf512:tf512;
          if(k>=lo&&k<=hi) s+=w; }
      p512=n?s/tot:1.0; }

    printf("ROW\tmassive-60\ttwin-ternary\t%.1f\t0.0\t0\t0\t%d\t%.0f\t0\t%d\n",
           100.0*twin_ok/Te_n, Te_n-twin_ok,
           (double)(Tr_n*sizeof(tvec))/1024, Te_n);
    printf("ROW\tmassive-60\tbinary-256\t%.1f\t0.0\t0\t0\t%d\t%.0f\t0\t%d\n",
           100.0*b256_ok/Te_n, Te_n-b256_ok,
           (double)(Tr_n*(256/32)*4)/1024, Te_n);
    printf("ROW\tmassive-60\tbinary-512\t%.1f\t0.0\t0\t0\t%d\t%.0f\t0\t%d\n",
           100.0*b512_ok/Te_n, Te_n-b512_ok,
           (double)(Tr_n*w512*4)/1024, Te_n);

    printf("\n  MASSIVE 60-class NN accuracy (n=%d test):\n",Te_n);
    printf("    twin-ternary  d=256  64 B/vec  %6.1f%%  (%d/%d)\n",
           100.0*twin_ok/Te_n,twin_ok,Te_n);
    printf("    binary        d=256  32 B/vec  %6.1f%%  (%d/%d)\n",
           100.0*b256_ok/Te_n,b256_ok,Te_n);
    printf("    binary        d=512  64 B/vec  %6.1f%%  (%d/%d)  [size-matched]\n",
           100.0*b512_ok/Te_n,b512_ok,Te_n);
    printf("\n  paired vs binary d=256:  twin fixed %d, binary fixed %d   p = %.4f %s\n",
           tf256,bf256,p256,p256<0.05?"SIGNIFICANT":"(not significant)");
    printf("  paired vs binary d=512:  twin fixed %d, binary fixed %d   p = %.4f %s\n",
           tf512,bf512,p512,p512<0.05?"SIGNIFICANT":"(not significant)");

    free(Tr); free(Te); free(T_idx); free(T_act); free(B256); free(B512);
    free(tp); free(bp256); free(bp512);
    return 0;
}