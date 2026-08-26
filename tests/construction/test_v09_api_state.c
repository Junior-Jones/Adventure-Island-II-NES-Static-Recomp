#include "include/ai2_static_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long long checks=0;
#define CK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);return 1;}}while(0)

typedef struct HookState {
    Ai2StaticCore *core;
    unsigned before_count,after_count,bus_read_count,bus_write_count,frame_count,frontier_count;
    unsigned read4016_count,write4016_count;
    int first_4016_bit;
    int capture_after_latch;
    int stop_before;
    int stop_frame;
    int attempt_reentry;
    int reentry_result;
    Ai2CoreStatus reentry_status;
} HookState;

static Ai2RuntimeHookAction hook_cb(const Ai2RuntimeEvent *e,void *user)
{
    HookState *h=(HookState*)user;
    Ai2FrameResult r;
    if(e->type==AI2_RUNTIME_EVENT_INSTRUCTION_BEFORE){
        h->before_count++;
        if(h->attempt_reentry&&h->before_count==1u){
            h->reentry_result=ai2_static_core_advance_frame(h->core,0,0,1,&r);
            h->reentry_status=ai2_static_core_status(h->core);
        }
        if(h->stop_before)return AI2_RUNTIME_HOOK_STOP;
    } else if(e->type==AI2_RUNTIME_EVENT_INSTRUCTION_AFTER) h->after_count++;
    else if(e->type==AI2_RUNTIME_EVENT_BUS_READ){
        h->bus_read_count++;
        if(e->bus_address==0x4016u){
            h->read4016_count++;
            if(h->capture_after_latch&&h->first_4016_bit<0){h->first_4016_bit=e->bus_value&1u;h->capture_after_latch=0;}
        }
    } else if(e->type==AI2_RUNTIME_EVENT_BUS_WRITE){
        h->bus_write_count++;
        if(e->bus_address==0x4016u){h->write4016_count++;if((e->bus_value&1u)==0u)h->capture_after_latch=1;}
    } else if(e->type==AI2_RUNTIME_EVENT_FRAME_COMPLETE){h->frame_count++;if(h->stop_frame)return AI2_RUNTIME_HOOK_STOP;}
    else if(e->type==AI2_RUNTIME_EVENT_FRONTIER)h->frontier_count++;
    return AI2_RUNTIME_HOOK_CONTINUE;
}

static unsigned char *read_file(const char *path,size_t *size)
{
    FILE*f=fopen(path,"rb");long n;unsigned char*p;if(!f)return NULL;
    if(fseek(f,0,SEEK_END)!=0||(n=ftell(f))<0||fseek(f,0,SEEK_SET)!=0){fclose(f);return NULL;}
    p=(unsigned char*)malloc((size_t)n? (size_t)n:1u);if(!p){fclose(f);return NULL;}
    if((size_t)n&&fread(p,1,(size_t)n,f)!=(size_t)n){free(p);fclose(f);return NULL;}fclose(f);*size=(size_t)n;return p;
}
static int write_file(const char *path,const unsigned char*p,size_t n){FILE*f=fopen(path,"wb");if(!f)return 0;int ok=fwrite(p,1,n,f)==n&&fclose(f)==0;return ok;}
static int same_file(const char*a,const char*b){size_t na,nb;unsigned char*pa=read_file(a,&na),*pb=read_file(b,&nb);int ok=pa&&pb&&na==nb&&memcmp(pa,pb,na)==0;free(pa);free(pb);return ok;}

static int test_bad_rom_and_public_copy(const unsigned char *rom,size_t n)
{
    Ai2StaticCore*c=ai2_static_core_create();unsigned char*bad=(unsigned char*)malloc(n);uint32_t *bgra;uint16_t *idx;Ai2FrameResult r;Ai2CoreObservation o;
    CK(c!=NULL);CK(bad!=NULL);memcpy(bad,rom,n);bad[n-1]^=1u;
    CK(!ai2_static_core_reset(c,bad,n));CK(ai2_static_core_status(c)==AI2_CORE_STATUS_BAD_ROM);
    CK(ai2_static_core_reset(c,rom,n));CK(strcmp(ai2_static_core_rom_sha256(),"10dd53fd62c95e77856b09c693b3bdd14b62a89988902eea111257cafe9b8acc")==0);
    CK(strlen(ai2_static_core_compatibility_sha256())==64u);
    CK(ai2_static_core_advance_frame(c,0,0,0,&r));CK(r.completed&&r.end_frame==1u);CK(!r.trap.runtime_trap_code);
    bgra=(uint32_t*)calloc(AI2_CORE_FRAME_PIXELS,sizeof(*bgra));idx=(uint16_t*)calloc(AI2_CORE_FRAME_PIXELS,sizeof(*idx));CK(bgra&&idx);
    CK(ai2_static_core_frame_copy_bgra(c,bgra,AI2_CORE_FRAME_PIXELS));CK(ai2_static_core_frame_copy_indexed(c,idx,AI2_CORE_FRAME_PIXELS));
    CK(!ai2_static_core_frame_copy_bgra(c,bgra,AI2_CORE_FRAME_PIXELS-1u));
    CK(ai2_static_core_observe(c,&o));CK(o.frames==1u&&o.cpu_cycles>0u&&o.ppu_cycles==o.cpu_cycles*3u&&o.apu_cycles==o.cpu_cycles);CK(o.pcm_available>0u);
    free(bgra);free(idx);free(bad);ai2_static_core_destroy(c);return 0;
}

static int controller_first_bit(const unsigned char *rom,size_t n,uint8_t buttons,int *bit,unsigned *reads,unsigned *writes)
{
    Ai2StaticCore*c=ai2_static_core_create();HookState h;Ai2FrameResult r;unsigned i;
    if(!c||!ai2_static_core_reset(c,rom,n)){ai2_static_core_destroy(c);return 0;}
    memset(&h,0,sizeof(h));h.core=c;h.first_4016_bit=-1;
    ai2_static_core_set_runtime_hook(c,AI2_RUNTIME_EVENT_BUS_READ|AI2_RUNTIME_EVENT_BUS_WRITE,hook_cb,&h);
    for(i=0;i<4u&&h.first_4016_bit<0;i++)if(!ai2_static_core_advance_frame(c,buttons,0,0,&r)){ai2_static_core_destroy(c);return 0;}
    *bit=h.first_4016_bit;*reads=h.read4016_count;*writes=h.write4016_count;ai2_static_core_destroy(c);return h.first_4016_bit>=0;
}

static int test_controller_latch(const unsigned char *rom,size_t n)
{
    int b0=-1,b1=-1;unsigned r0=0,w0=0,r1=0,w1=0;
    CK(controller_first_bit(rom,n,0u,&b0,&r0,&w0));CK(controller_first_bit(rom,n,0x01u,&b1,&r1,&w1));
    CK(r0>0u&&w0>0u&&r1>0u&&w1>0u);CK(b0==0);CK(b1==1);return 0;
}

static int test_hooks(const unsigned char *rom,size_t n)
{
    Ai2StaticCore*c=ai2_static_core_create();HookState h;Ai2FrameResult r;
    CK(c&&ai2_static_core_reset(c,rom,n));memset(&h,0,sizeof(h));h.core=c;h.first_4016_bit=-1;h.attempt_reentry=1;h.stop_before=1;
    ai2_static_core_set_runtime_hook(c,AI2_RUNTIME_EVENT_INSTRUCTION_BEFORE,hook_cb,&h);
    CK(ai2_static_core_advance_frame(c,0,0,0,&r));CK(r.stopped&&!r.completed);CK(h.before_count==1u);CK(h.reentry_result==0);CK(h.reentry_status==AI2_CORE_STATUS_REENTRANT_CALL);
    ai2_static_core_resume(c);CK(!ai2_static_core_is_stopped(c));
    memset(&h,0,sizeof(h));h.core=c;h.first_4016_bit=-1;
    ai2_static_core_set_runtime_hook(c,AI2_RUNTIME_EVENT_BUS_READ|AI2_RUNTIME_EVENT_BUS_WRITE,hook_cb,&h);
    CK(ai2_static_core_advance_frame(c,0,0,0,&r));CK(h.bus_read_count>0u&&h.bus_write_count>0u);CK(h.before_count==0u&&h.after_count==0u&&h.frame_count==0u);
    memset(&h,0,sizeof(h));h.core=c;h.first_4016_bit=-1;h.stop_frame=1;
    ai2_static_core_set_runtime_hook(c,AI2_RUNTIME_EVENT_FRAME_COMPLETE,hook_cb,&h);
    CK(ai2_static_core_advance_frame(c,0,0,0,&r));CK(r.completed&&r.stopped);CK(h.frame_count==1u);CK(ai2_static_core_is_stopped(c));
    ai2_static_core_resume(c);ai2_static_core_clear_runtime_hook(c);ai2_static_core_destroy(c);return 0;
}

static int test_snapshots(const unsigned char *rom,size_t n,const char *dir)
{
    char a[512],b[512],corrupt[512],rombad[512],corebad[512],trail[512],err[256];Ai2StaticCore*c=ai2_static_core_create();Ai2FrameResult r;unsigned char*p;size_t sz;FILE*f;
    snprintf(a,sizeof(a),"%s/state-a.ai2state",dir);snprintf(b,sizeof(b),"%s/state-b.ai2state",dir);snprintf(corrupt,sizeof(corrupt),"%s/state-corrupt.ai2state",dir);snprintf(rombad,sizeof(rombad),"%s/state-rombad.ai2state",dir);snprintf(corebad,sizeof(corebad),"%s/state-corebad.ai2state",dir);snprintf(trail,sizeof(trail),"%s/state-trail.ai2state",dir);
    CK(c&&ai2_static_core_reset(c,rom,n));CK(ai2_static_core_advance_frame(c,0x01u,0,0,&r));CK(ai2_static_core_advance_frame(c,0x80u,0,0,&r));
    CK(ai2_static_core_snapshot_save(c,a,err,sizeof(err))==AI2_SNAPSHOT_OK);
    CK(ai2_static_core_advance_frame(c,0,0,0,&r));CK(ai2_static_core_snapshot_load(c,a,err,sizeof(err))==AI2_SNAPSHOT_OK);
    CK(ai2_static_core_snapshot_save(c,b,err,sizeof(err))==AI2_SNAPSHOT_OK);CK(same_file(a,b));
    p=read_file(a,&sz);CK(p&&sz>140u);
    p[130]^=0x40u;CK(write_file(corrupt,p,sz));p[130]^=0x40u;
    p[48]^=1u;CK(write_file(rombad,p,sz));p[48]^=1u;
    p[16]^=1u;CK(write_file(corebad,p,sz));p[16]^=1u;
    CK(write_file(trail,p,sz));f=fopen(trail,"ab");CK(f!=NULL);fputc(0,f);CK(fclose(f)==0);free(p);
    CK(ai2_static_core_snapshot_load(c,corrupt,err,sizeof(err))==AI2_SNAPSHOT_PAYLOAD_HASH_MISMATCH);CK(ai2_static_core_snapshot_save(c,b,err,sizeof(err))==AI2_SNAPSHOT_OK);CK(same_file(a,b));
    CK(ai2_static_core_snapshot_load(c,rombad,err,sizeof(err))==AI2_SNAPSHOT_ROM_MISMATCH);CK(ai2_static_core_snapshot_save(c,b,err,sizeof(err))==AI2_SNAPSHOT_OK);CK(same_file(a,b));
    CK(ai2_static_core_snapshot_load(c,corebad,err,sizeof(err))==AI2_SNAPSHOT_CORE_MISMATCH);CK(ai2_static_core_snapshot_save(c,b,err,sizeof(err))==AI2_SNAPSHOT_OK);CK(same_file(a,b));
    CK(ai2_static_core_snapshot_load(c,trail,err,sizeof(err))==AI2_SNAPSHOT_TRAILING_DATA);CK(ai2_static_core_snapshot_save(c,b,err,sizeof(err))==AI2_SNAPSHOT_OK);CK(same_file(a,b));
    ai2_static_core_destroy(c);return 0;
}

int main(int argc,char**argv)
{
    size_t n=0;unsigned char*rom;if(argc!=3){fprintf(stderr,"usage: %s ROM.nes tempdir\n",argv[0]);return 2;}rom=read_file(argv[1],&n);CK(rom!=NULL);CK(n==262160u);
    if(test_bad_rom_and_public_copy(rom,n)||test_controller_latch(rom,n)||test_hooks(rom,n)||test_snapshots(rom,n,argv[2])){free(rom);return 1;}
    free(rom);printf("V09 opaque API/snapshot/hook construction: PASS (%llu checks)\n",checks);return 0;
}
