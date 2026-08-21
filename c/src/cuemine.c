/* cuemine.c — which WORDS discriminate a class, mined from the INDEX ONLY.
 *
 * Purpose: the residual wrong-action errors cluster into patterns (colour terms
 * routed away from lightchange; "close" not recognised as an off-cue). The
 * temptation is to read the dev failures and hand-write a lexicon for exactly
 * those cases. That is precisely the archived failure — a lexicon fitted to
 * observed failures reported +4.5 points and the leakage-free version gained
 * nothing (see archive/ and doc/METHOD.md).
 *
 * So: candidate cues must come from TRAINING data, never from the dev errors.
 * This replicates compare.c's DEV carve exactly (1-in-4 IoT, 1-in-8 negatives,
 * dedup against test first) and mines only what remains — the index.
 *
 * Reports lift = P(class|word) / P(class), which is what makes a word
 * discriminative rather than merely frequent.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "router.h"

#define MAXW 40000
#define MAXC 16
static char  W[MAXW][24];
static int   Wc[MAXW][MAXC], Wt[MAXW], Wn;
static int   Cn[MAXC], Ntot, NC;
static char  CN[MAXC][RNAMELEN];
#define HN 65536
static char *HS[HN];

static int js(const char*l,const char*k,char*o,int cap){
    char pat[64]; snprintf(pat,sizeof pat,"\"%s\":",k);
    const char*p=strstr(l,pat); if(!p)return 0; p+=strlen(pat);
    while(*p==' ')p++; if(*p!='"')return 0; p++;
    int n=0; while(*p&&*p!='"'&&n<cap-1){if(*p=='\\'&&p[1])p++;o[n++]=*p++;} o[n]=0; return 1; }
static int isiot(const char*l){return !strncmp(l,"iot_",4);}
static unsigned h32(const char*s){ unsigned h=2166136261u; while(*s){h^=(unsigned char)*s++;h*=16777619u;} return h; }
static void hs_add(const char*s){ char b[512]; r_norm(s,b,sizeof b);
    unsigned i=h32(b)&(HN-1); while(HS[i]){ if(!strcmp(HS[i],b))return; i=(i+1)&(HN-1);} HS[i]=strdup(b); }
static int hs_has(const char*s){ char b[512]; r_norm(s,b,sizeof b);
    unsigned i=h32(b)&(HN-1); while(HS[i]){ if(!strcmp(HS[i],b))return 1; i=(i+1)&(HN-1);} return 0; }

static int cls_id(const char *n){
    for(int i=0;i<NC;i++) if(!strcmp(CN[i],n)) return i;
    snprintf(CN[NC],RNAMELEN,"%s",n); return NC++; }
static int word_id(const char *w){
    for(int i=0;i<Wn;i++) if(!strcmp(W[i],w)) return i;
    if(Wn>=MAXW) return -1;
    snprintf(W[Wn],24,"%s",w); return Wn++; }
static void add(const char *text, int c){
    char b[512]; r_norm(text,b,sizeof b);
    char *p=b, tok[24]; int n=0;
    for(;;p++){
        if(*p && *p!=' '){ if(n<23) tok[n++]=*p; continue; }
        if(n){ tok[n]=0; int w=word_id(tok); if(w>=0){ Wc[w][c]++; Wt[w]++; } n=0; }
        if(!*p) break;
    }
    Cn[c]++; Ntot++;
}
int main(int argc,char**argv){
    if(argc<4){ fprintf(stderr,
        "usage: cuemine <train.json> <test.json> <class> [min_count]\n"
        "  Top words by lift for <class>, mined from the INDEX ONLY —\n"
        "  compare.c's DEV carve is replicated so dev never contributes.\n"
        "  Candidate cues MUST come from here, not from reading dev errors.\n"); return 1; }
    int minc = argc>4 ? atoi(argv[4]) : 4;
    char line[8192],t[512],l[RNAMELEN]; FILE*f;
    f=fopen(argv[2],"r"); if(!f){perror(argv[2]);return 1;}
    while(fgets(line,sizeof line,f)) if(js(line,"text",t,sizeof t)) hs_add(t);
    fclose(f);
    f=fopen(argv[1],"r"); if(!f){perror(argv[1]);return 1;}
    int ti=0,tn=0,ndev=0;
    while(fgets(line,sizeof line,f)) if(js(line,"text",t,sizeof t)&&js(line,"label_text",l,sizeof l)){
        int io=isiot(l);
        if(hs_has(t)) continue;
        if(io && (ti++ % 4)==0){ hs_add(t); ndev++; continue; }      /* DEV — excluded */
        if(!io && (tn++ % 8)==0){ hs_add(t); ndev++; continue; }     /* DEV — excluded */
        hs_add(t); add(t, cls_id(io?l:"none"));
    }
    fclose(f);
    int c=-1; for(int i=0;i<NC;i++) if(!strcmp(CN[i],argv[3])) c=i;
    if(c<0){ fprintf(stderr,"  no such class. present:"); for(int i=0;i<NC;i++) fprintf(stderr," %s",CN[i]); fprintf(stderr,"\n"); return 1; }
    fprintf(stderr,"  index %d utterances (%d dev excluded), class %s has %d\n",
            Ntot, ndev, CN[c], Cn[c]);
    double base = (double)Cn[c]/Ntot;
    printf("  %-14s %-6s %-6s %-8s %s\n","word","in_cls","total","P(c|w)","lift");
    for(int r=0;r<18;r++){
        int bi=-1; double bl=0;
        for(int i=0;i<Wn;i++){
            if(Wt[i]<minc || Wc[i][c]==0) continue;
            double lift = ((double)Wc[i][c]/Wt[i])/base;
            if(lift>bl){ bl=lift; bi=i; }
        }
        if(bi<0) break;
        printf("  %-14s %-6d %-6d %-8.2f %.1fx\n", W[bi], Wc[bi][c], Wt[bi],
               (double)Wc[bi][c]/Wt[bi], bl);
        Wt[bi]=0;   /* consume */
    }
    return 0;
}
