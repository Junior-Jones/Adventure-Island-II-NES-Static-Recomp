#define _POSIX_C_SOURCE 200809L
#include "frontend/common/ai2_frontend_session.h"
#include "frontend/common/ai2_frontend_capture.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#if defined(_WIN32)
#include <direct.h>
#endif

#define AI2_NTSC_CPU_HZ 1789773ull
#define AI2_PCM_HZ 44100u
#define MAX_SCREENSHOT_FRAMES 128u

typedef struct Options {
    const char *rom;
    uint64_t frames;
    double seconds;
    const char *screenshot_dir;
    uint64_t screenshot_frames[MAX_SCREENSHOT_FRAMES];
    size_t screenshot_count;
    uint64_t screenshot_every;
    const char *wav_path;
    uint64_t status_every;
} Options;

static void usage(const char *exe)
{
    fprintf(stderr,
        "usage: %s ROM.nes [--frames N | --seconds S] [--screenshot-dir DIR] "
        "[--screenshot-frame N] [--screenshot-every N] [--wav FILE] [--status-every N]\n",exe);
}

static int parse_u64(const char *s,uint64_t *v){char *e=NULL;unsigned long long x;if(!s||!*s)return 0;errno=0;x=strtoull(s,&e,10);if(errno||!e||*e)return 0;*v=(uint64_t)x;return 1;}
static int parse_double(const char *s,double *v){char *e=NULL;double x;if(!s||!*s)return 0;errno=0;x=strtod(s,&e);if(errno||!e||*e||x<=0.0)return 0;*v=x;return 1;}

static int parse_options(int argc,char **argv,Options *o)
{
    int i;if(!o||argc<2)return 0;memset(o,0,sizeof(*o));o->rom=argv[1];
    for(i=2;i<argc;i++){
        if(strcmp(argv[i],"--frames")==0&&i+1<argc){if(!parse_u64(argv[++i],&o->frames)||o->seconds>0.0)return 0;}
        else if(strcmp(argv[i],"--seconds")==0&&i+1<argc){if(!parse_double(argv[++i],&o->seconds)||o->frames)return 0;}
        else if(strcmp(argv[i],"--screenshot-dir")==0&&i+1<argc)o->screenshot_dir=argv[++i];
        else if(strcmp(argv[i],"--screenshot-frame")==0&&i+1<argc){if(o->screenshot_count>=MAX_SCREENSHOT_FRAMES||!parse_u64(argv[++i],&o->screenshot_frames[o->screenshot_count]))return 0;o->screenshot_count++;}
        else if(strcmp(argv[i],"--screenshot-every")==0&&i+1<argc){if(!parse_u64(argv[++i],&o->screenshot_every))return 0;}
        else if(strcmp(argv[i],"--wav")==0&&i+1<argc)o->wav_path=argv[++i];
        else if(strcmp(argv[i],"--status-every")==0&&i+1<argc){if(!parse_u64(argv[++i],&o->status_every))return 0;}
        else return 0;
    }
    if(!o->frames&&o->seconds<=0.0)o->frames=300u;
    if((o->screenshot_count||o->screenshot_every)&&!o->screenshot_dir)return 0;
    return 1;
}

static int make_dir(const char *path)
{
#if defined(_WIN32)
    struct _stat st;if(!path||!*path)return 0;if(_stat(path,&st)==0)return (st.st_mode&_S_IFDIR)!=0;return _mkdir(path)==0;
#else
    struct stat st;if(!path||!*path)return 0;if(stat(path,&st)==0)return S_ISDIR(st.st_mode);return mkdir(path,0775)==0;
#endif
}

static int should_capture(const Options *o, uint64_t frame)
{
    size_t i;
    if(o == NULL || o->screenshot_dir == NULL) return 0;
    if(o->screenshot_every != 0u && frame % o->screenshot_every == 0u) return 1;
    for(i = 0; i < o->screenshot_count; i++) {
        if(o->screenshot_frames[i] == frame) return 1;
    }
    return 0;
}

static int save_frame(Ai2FrontendSession *s,const Options *o,uint64_t frame,uint32_t *pixels)
{
    char path[1024];uint64_t hash;if(!ai2_frontend_session_copy_bgra(s,pixels,AI2_CORE_FRAME_PIXELS))return 0;
    if(snprintf(path,sizeof(path),"%s/ai2-frame-%06" PRIu64 ".bmp",o->screenshot_dir,frame)>=(int)sizeof(path))return 0;
    if(!ai2_frontend_write_bmp(path,pixels,AI2_CORE_FRAME_WIDTH,AI2_CORE_FRAME_HEIGHT))return 0;
    hash=ai2_frontend_frame_hash_bgra(pixels,AI2_CORE_FRAME_PIXELS);printf("SCREENSHOT frame=%" PRIu64 " hash=%016" PRIx64 " path=%s\n",frame,hash,path);fflush(stdout);return 1;
}

static int drain_audio(Ai2FrontendSession *s,Ai2WavWriter *wav,uint64_t *drained)
{
    int16_t pcm[4096];size_t got;do{got=ai2_frontend_session_audio_read(s,pcm,sizeof(pcm)/sizeof(pcm[0]));if(got){*drained+=got;if(wav&&wav->file&&!ai2_wav_append(wav,pcm,got))return 0;}}while(got);return 1;
}

int main(int argc,char **argv)
{
    Options o;Ai2FrontendSession s;Ai2FrameResult r;Ai2CoreObservation before,obs;Ai2WavWriter wav;
    uint32_t *pixels=NULL;uint64_t target_cpu=0,target_frame=0,drained=0;char error[256]={0};int rc=1,wav_open=0;
    if(!parse_options(argc,argv,&o)){usage(argv[0]);return 2;}
    if(o.screenshot_dir&&!make_dir(o.screenshot_dir)){fprintf(stderr,"could not create screenshot directory: %s\n",o.screenshot_dir);return 3;}
    if(!ai2_frontend_session_open(&s,o.rom,error,sizeof(error))){fprintf(stderr,"ROM/core open failed: %s\n",error);return 4;}
    if(!ai2_static_core_observe(s.core,&before)){fprintf(stderr,"initial observation failed\n");goto done;}
    if(o.frames)target_frame=before.frames+o.frames;else target_cpu=before.cpu_cycles+(uint64_t)(o.seconds*(double)AI2_NTSC_CPU_HZ+0.5);
    if(o.wav_path){if(!ai2_wav_open(&wav,o.wav_path,AI2_PCM_HZ)){fprintf(stderr,"could not open WAV: %s\n",o.wav_path);goto done;}wav_open=1;}
    pixels=(uint32_t*)malloc((size_t)AI2_CORE_FRAME_PIXELS*sizeof(*pixels));if(!pixels){fprintf(stderr,"frame allocation failed\n");goto done;}
    for(;;){
        if(o.frames){if(!ai2_static_core_observe(s.core,&obs)||obs.frames>=target_frame)break;}
        else{if(!ai2_static_core_observe(s.core,&obs)||obs.cpu_cycles>=target_cpu)break;}
        if(!ai2_frontend_session_advance(&s,0u,0u,&r)||!r.completed){Ai2CoreTrapInfo t;memset(&t,0,sizeof(t));ai2_static_core_trap(s.core,&t);fprintf(stderr,"CORE_STOP status=%s frame=%" PRIu64 " pc=%04X bank=%u trap=%u source=%04X address=%04X cpu=%" PRIu64 "\n",ai2_static_core_status_name(ai2_static_core_status(s.core)),r.end_frame,r.program_counter,r.physical_prg_bank,t.runtime_trap_code,t.source_pc,t.address,t.cpu_cycle);goto done;}
        if(!drain_audio(&s,wav_open?&wav:NULL,&drained)){fprintf(stderr,"audio drain/write failed\n");goto done;}
        if(should_capture(&o,r.end_frame)&&!save_frame(&s,&o,r.end_frame,pixels)){fprintf(stderr,"screenshot failed at frame %" PRIu64 "\n",r.end_frame);goto done;}
        if(o.status_every&&r.end_frame%o.status_every==0u){if(ai2_static_core_observe(s.core,&obs))printf("PROGRESS frame=%" PRIu64 " cpu=%" PRIu64 " pc=%04X bank=%u instructions=%" PRIu64 " samples=%" PRIu64 "\n",obs.frames,obs.cpu_cycles,obs.program_counter,obs.physical_prg_bank,obs.executed_instructions,drained);fflush(stdout);}
    }
    if(!drain_audio(&s,wav_open?&wav:NULL,&drained)){fprintf(stderr,"final audio drain/write failed\n");goto done;}
    if(!ai2_static_core_observe(s.core,&obs)||!ai2_frontend_session_copy_bgra(&s,pixels,AI2_CORE_FRAME_PIXELS)){fprintf(stderr,"final observation/frame copy failed\n");goto done;}
    printf("FINAL_STATUS mode=headless frames=%" PRIu64 " frame_delta=%" PRIu64 " cpu_cycles=%" PRIu64 " cpu_delta=%" PRIu64 " emulated_seconds=%.9f pc=%04X bank=%u instructions=%" PRIu64 " frame_hash=%016" PRIx64 " pcm_total=%" PRIu64 " pcm_drained=%" PRIu64 " pcm_available=%zu status=%s\n",
        obs.frames,obs.frames-before.frames,obs.cpu_cycles,obs.cpu_cycles-before.cpu_cycles,(double)(obs.cpu_cycles-before.cpu_cycles)/(double)AI2_NTSC_CPU_HZ,obs.program_counter,obs.physical_prg_bank,obs.executed_instructions,ai2_frontend_frame_hash_bgra(pixels,AI2_CORE_FRAME_PIXELS),obs.pcm_total_samples,drained,obs.pcm_available,ai2_static_core_status_name(obs.status));
    rc=0;
done:
    if(wav_open&&!ai2_wav_close(&wav)){fprintf(stderr,"WAV finalization failed\n");rc=1;}
    free(pixels);ai2_frontend_session_close(&s);return rc;
}
