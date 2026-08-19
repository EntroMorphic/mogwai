/* tinyenc on ESP32 — int8/int32 arithmetic, weights in SRAM, both cores.
 * Measures four configurations so each optimisation is attributable. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "ref_vectors.h"

extern const uint8_t blob_start[] asm("_binary_tinyenc_bin_start");
extern const uint8_t blob_end[]   asm("_binary_tinyenc_bin_end");

#define V 8192
#define D 64
#define F 256
#define L 2
#define H 4
#define S 24
#define HD (D/H)

typedef struct { float scale; const int8_t *q; } tensor_t;
static tensor_t T_emb, T_pos, T_b[L][8];
static const uint8_t *cur;
static tensor_t take(size_t n){ tensor_t t; memcpy(&t.scale,cur,4); cur+=4; t.q=(const int8_t*)cur; cur+=n; return t; }
static int parse_blob(void){
    cur=blob_start;
    if(memcmp(cur,"TENC",4)) return -1;
    cur+=4;
    uint16_t h[6];
    memcpy(h,cur,12);
    cur+=12;
    if(h[0]!=V||h[1]!=D||h[2]!=F||h[3]!=L||h[4]!=H||h[5]!=S) return -2;
    T_emb=take((size_t)V*D); T_pos=take((size_t)S*D);
    for(int i=0;i<L;i++){
        T_b[i][0]=take(D*D); T_b[i][1]=take(D*D); T_b[i][2]=take(D*D); T_b[i][3]=take(D*D);
        T_b[i][4]=take((size_t)D*F); T_b[i][5]=take((size_t)F*D);
        T_b[i][6]=take(D); T_b[i][7]=take(D);
    }
    return cur==blob_end?0:-3;
}

/* ---- job: one matmul over a row range, executable on either core ---- */
typedef struct { const float *in; int din,dout; const tensor_t *w; float *out; int r0,r1; } job_t;

static void mm_rows_i8(const job_t *j){
    int8_t  qa[F];
    int32_t acc[F];
    for(int t=j->r0;t<j->r1;t++){
        const float *x=j->in+(size_t)t*j->din;
        float m=0.f; for(int d=0;d<j->din;d++){ float a=fabsf(x[d]); if(a>m) m=a; }
        float sa;
        if(m==0.f){ memset(qa,0,j->din); sa=0.f; }
        else { float inv=127.0f/m; sa=m/127.0f;
               for(int d=0;d<j->din;d++) qa[d]=(int8_t)lrintf(x[d]*inv); }
        memset(acc,0,(size_t)j->dout*sizeof(int32_t));
        for(int d=0;d<j->din;d++){
            int32_t a=qa[d]; if(!a) continue;
            const int8_t *wr=j->w->q+(size_t)d*j->dout;
            for(int n=0;n<j->dout;n++) acc[n]+=a*(int32_t)wr[n];
        }
        float sc=sa*j->w->scale; float *o=j->out+(size_t)t*j->dout;
        for(int n=0;n<j->dout;n++) o[n]=(float)acc[n]*sc;
    }
}
/* weights transposed to [out][in]: accumulator lives in a register and the
   weight stream is sequential. this is the load-store fix, not an arith fix. */
static void mm_rows_i8T(const job_t *j){
    int8_t qa[F];
    for(int t=j->r0;t<j->r1;t++){
        const float *x=j->in+(size_t)t*j->din;
        float m=0.f; for(int d=0;d<j->din;d++){ float a=fabsf(x[d]); if(a>m) m=a; }
        float sa;
        if(m==0.f){ memset(qa,0,j->din); sa=0.f; }
        else { float inv=127.0f/m; sa=m/127.0f;
               for(int d=0;d<j->din;d++) qa[d]=(int8_t)lrintf(x[d]*inv); }
        float sc=sa*j->w->scale; float *o=j->out+(size_t)t*j->dout;
        for(int n=0;n<j->dout;n++){
            const int8_t *wr=j->w->q+(size_t)n*j->din;
            int32_t s0=0,s1=0,s2=0,s3=0; int d=0;
            for(; d+4<=j->din; d+=4){
                s0+=(int32_t)qa[d]  *(int32_t)wr[d];
                s1+=(int32_t)qa[d+1]*(int32_t)wr[d+1];
                s2+=(int32_t)qa[d+2]*(int32_t)wr[d+2];
                s3+=(int32_t)qa[d+3]*(int32_t)wr[d+3];
            }
            int32_t s=s0+s1+s2+s3;
            for(; d<j->din; d++) s+=(int32_t)qa[d]*(int32_t)wr[d];
            o[n]=(float)s*sc;
        }
    }
}

/* [in][out] layout (keeps the zero-activation skip) but with 8 accumulators
   held in registers, so the inner loop does one byte-load per MAC and no
   accumulator load/store at all. */
static void mm_rows_i8B(const job_t *j){
    int8_t qa[F];
    for(int t=j->r0;t<j->r1;t++){
        const float *x=j->in+(size_t)t*j->din;
        float m=0.f; for(int d=0;d<j->din;d++){ float a=fabsf(x[d]); if(a>m) m=a; }
        float sa;
        if(m==0.f){ memset(qa,0,j->din); sa=0.f; }
        else { float inv=127.0f/m; sa=m/127.0f;
               for(int d=0;d<j->din;d++) qa[d]=(int8_t)lrintf(x[d]*inv); }
        float sc=sa*j->w->scale; float *o=j->out+(size_t)t*j->dout;
        for(int n0=0;n0<j->dout;n0+=8){
            int32_t a0=0,a1=0,a2=0,a3=0,a4=0,a5=0,a6=0,a7=0;
            for(int d=0;d<j->din;d++){
                int32_t a=qa[d]; if(!a) continue;
                const int8_t *wr=j->w->q+(size_t)d*j->dout+n0;
                a0+=a*(int32_t)wr[0]; a1+=a*(int32_t)wr[1];
                a2+=a*(int32_t)wr[2]; a3+=a*(int32_t)wr[3];
                a4+=a*(int32_t)wr[4]; a5+=a*(int32_t)wr[5];
                a6+=a*(int32_t)wr[6]; a7+=a*(int32_t)wr[7];
            }
            o[n0]=(float)a0*sc; o[n0+1]=(float)a1*sc; o[n0+2]=(float)a2*sc; o[n0+3]=(float)a3*sc;
            o[n0+4]=(float)a4*sc; o[n0+5]=(float)a5*sc; o[n0+6]=(float)a6*sc; o[n0+7]=(float)a7*sc;
        }
    }
}

static void mm_rows_f32(const job_t *j){
    for(int t=j->r0;t<j->r1;t++){
        const float *x=j->in+(size_t)t*j->din; float *o=j->out+(size_t)t*j->dout;
        for(int n=0;n<j->dout;n++) o[n]=0.f;
        for(int d=0;d<j->din;d++){
            float xv=x[d]; if(xv==0.f) continue;
            const int8_t *wr=j->w->q+(size_t)d*j->dout;
            for(int n=0;n<j->dout;n++) o[n]+=xv*(float)wr[n];
        }
        for(int n=0;n<j->dout;n++) o[n]*=j->w->scale;
    }
}

static volatile job_t g_job;
static SemaphoreHandle_t sem_go, sem_done;
static volatile int g_int8=1;
static volatile int g_trans=0;
static volatile int g_blk=0;
static void worker(void *arg){
    (void)arg;
    for(;;){ xSemaphoreTake(sem_go,portMAX_DELAY);
             __atomic_thread_fence(__ATOMIC_ACQUIRE);
             job_t j=*(job_t*)&g_job;
             if(g_blk) mm_rows_i8B(&j); else if(g_trans) mm_rows_i8T(&j); else if(g_int8) mm_rows_i8(&j); else mm_rows_f32(&j);
             xSemaphoreGive(sem_done); }
}

static int g_dual=0;
static void matmul(const float *in,int rows,int din,const tensor_t *w,int dout,float *out){
    job_t j={in,din,dout,w,out,0,rows};
    if(g_dual && rows>=2){
        int mid=rows/2;
        job_t jb={in,din,dout,w,out,mid,rows};
        *(job_t*)&g_job=jb;
        /* volatile is not a barrier: without this the worker on core 1 may
           observe a stale or torn g_job. release-store before signalling. */
        __atomic_thread_fence(__ATOMIC_RELEASE);
        xSemaphoreGive(sem_go);
        j.r1=mid; if(g_blk) mm_rows_i8B(&j); else if(g_trans) mm_rows_i8T(&j); else if(g_int8) mm_rows_i8(&j); else mm_rows_f32(&j);
        xSemaphoreTake(sem_done,portMAX_DELAY);
    } else { if(g_blk) mm_rows_i8B(&j); else if(g_trans) mm_rows_i8T(&j); else if(g_int8) mm_rows_i8(&j); else mm_rows_f32(&j); }
}

static float xb[S*D],hb[S*D],qb[S*D],kb[S*D],vb[S*D],ob[S*D],fb[S*F],att[S];
static void rmsnorm(const float *x,int rows,const tensor_t *g,float *out){
    for(int t=0;t<rows;t++){
        const float *r=x+(size_t)t*D; float *o=out+(size_t)t*D;
        float s=0.f; for(int i=0;i<D;i++) s+=r[i]*r[i];
        float inv=1.0f/sqrtf(s/D+1e-6f);
        for(int i=0;i<D;i++) o[i]=(float)g->q[i]*g->scale*r[i]*inv;
    }
}
static void encode(const int16_t *ids,int len,float *outv){
    for(int t=0;t<len;t++) for(int d=0;d<D;d++)
        xb[t*D+d]=(float)T_emb.q[(size_t)ids[t]*D+d]*T_emb.scale
                 +(float)T_pos.q[(size_t)t*D+d]*T_pos.scale;
    for(int l=0;l<L;l++){
        rmsnorm(xb,len,&T_b[l][6],hb);
        matmul(hb,len,D,&T_b[l][0],D,qb);
        matmul(hb,len,D,&T_b[l][1],D,kb);
        matmul(hb,len,D,&T_b[l][2],D,vb);
        for(int h=0;h<H;h++) for(int i=0;i<len;i++){
            float mx=-1e30f;
            for(int j=0;j<len;j++){ float s=0.f;
                for(int d=0;d<HD;d++) s+=qb[i*D+h*HD+d]*kb[j*D+h*HD+d];
                att[j]=s/sqrtf((float)HD); if(att[j]>mx) mx=att[j]; }
            float sum=0.f; for(int j=0;j<len;j++){ att[j]=expf(att[j]-mx); sum+=att[j]; }
            for(int d=0;d<HD;d++){ float a=0.f;
                for(int j=0;j<len;j++) a+=att[j]*vb[j*D+h*HD+d];
                ob[i*D+h*HD+d]=a/sum; }
        }
        matmul(ob,len,D,&T_b[l][3],D,hb);
        for(int i=0;i<len*D;i++) xb[i]+=hb[i];
        rmsnorm(xb,len,&T_b[l][7],hb);
        matmul(hb,len,D,&T_b[l][4],F,fb);
        for(int i=0;i<len*F;i++) if(fb[i]<0.f) fb[i]=0.f;
        matmul(fb,len,F,&T_b[l][5],D,hb);
        for(int i=0;i<len*D;i++) xb[i]+=hb[i];
    }
    for(int d=0;d<D;d++){ float s=0.f; for(int t=0;t<len;t++) s+=xb[t*D+d]; outv[d]=s/len; }
    float n=0.f; for(int d=0;d<D;d++) n+=outv[d]*outv[d];
    n=1.0f/(sqrtf(n)+1e-6f); for(int d=0;d<D;d++) outv[d]*=n;
}

static void run(const char *label){
    float out[D]; int64_t tot=0; float worst=1.0f;
    for(int n=0;n<REF_N;n++){
        int64_t t0=esp_timer_get_time();
        encode(ref_ids[n],ref_len[n],out);
        tot+=esp_timer_get_time()-t0;
        float dot=0.f; for(int d=0;d<D;d++) dot+=out[d]*ref_vec[n][d];
        if(dot<worst) worst=dot;
    }
    printf("  %-34s %7lld us total   cos(dev,host) >= %.6f\n",label,tot,worst);
}

void app_main(void){
    esp_chip_info_t ci; esp_chip_info(&ci);
    printf("\n===== tinyenc: moving work to the metal =====\n");
    printf("chip %d cores, free heap %lu\n",ci.cores,(unsigned long)esp_get_free_heap_size());
    if(parse_blob()){ printf("blob parse FAILED\n"); return; }

    sem_go=xSemaphoreCreateBinary(); sem_done=xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(worker,"mmw",4096,NULL,5,NULL,1);

    printf("\n  --- weights in FLASH ---\n");
    g_int8=0; g_dual=0; run("A  float32, 1 core");
    g_int8=1; g_dual=0; run("B  int8/int32, 1 core");

    size_t need=0; for(int l=0;l<L;l++) need+=4*D*D+(size_t)D*F+(size_t)F*D+2*D;
    int8_t *sram=heap_caps_malloc(need,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    printf("\n  --- weights copied to SRAM (%u bytes, %s) ---\n",(unsigned)need,sram?"ok":"FAILED");
    if(sram){
        int8_t *w=sram; const size_t sz[8]={D*D,D*D,D*D,D*D,(size_t)D*F,(size_t)F*D,D,D};
        for(int l=0;l<L;l++) for(int k=0;k<8;k++){ memcpy(w,T_b[l][k].q,sz[k]); T_b[l][k].q=w; w+=sz[k]; }
        g_int8=1; g_dual=0; run("C  int8/int32, SRAM, 1 core");
        g_int8=1; g_dual=1; run("D  int8/int32, SRAM, 2 cores");
    }
    /* second copy, transposed [out][in] */
    int8_t *sramT=heap_caps_malloc(need,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    printf("\n  --- weights transposed to [out][in] in SRAM (%s) ---\n",sramT?"ok":"FAILED");
    if(sramT){
        int8_t *w=sramT;
        const int din[8] ={D,D,D,D,D,F,1,1};
        const int dout[8]={D,D,D,D,F,D,D,D};
        for(int l=0;l<L;l++) for(int k=0;k<8;k++){
            size_t n=(size_t)din[k]*dout[k];
            if(k>=6){ memcpy(w,T_b[l][k].q,D); }
            else { const int8_t *src=T_b[l][k].q;
                   for(int o2=0;o2<dout[k];o2++) for(int i2=0;i2<din[k];i2++)
                       w[(size_t)o2*din[k]+i2]=src[(size_t)i2*dout[k]+o2]; }
            T_b[l][k].q=w; w+= (k>=6)? D : n;
        }
        g_int8=1; g_trans=1; g_dual=0; run("E  int8 transposed, SRAM, 1 core");
        g_dual=1;                      run("F  int8 transposed, SRAM, 2 cores");
    }
    if(sram){
        int8_t *w=sram; const size_t sz[8]={D*D,D*D,D*D,D*D,(size_t)D*F,(size_t)F*D,D,D};
        for(int l=0;l<L;l++) for(int k=0;k<8;k++){ T_b[l][k].q=w; w+=sz[k]; }
        printf("\n  --- register-tiled kernel, [in][out] SRAM ---\n");
        g_trans=0; g_blk=1; g_int8=1; g_dual=0; run("G  int8 tiled, SRAM, 1 core");
        g_dual=1;                               run("H  int8 tiled, SRAM, 2 cores");
    }
    printf("\n  free heap now %lu\n",(unsigned long)esp_get_free_heap_size());
    printf("\n===== done =====\n");
    while(1) vTaskDelay(pdMS_TO_TICKS(1000));
}
