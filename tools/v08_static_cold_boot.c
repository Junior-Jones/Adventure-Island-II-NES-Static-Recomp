#include "src/runtime/ai2_v06_runtime.h"
#include "src/ppu/ai2_v07_ppu.h"
#include "src/apu/ai2_v08_apu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *load_file(const char *path,size_t *size)
{
    FILE *f=fopen(path,"rb"); long n; unsigned char *p;
    if(!f)return NULL;
    if(fseek(f,0,SEEK_END)!=0||(n=ftell(f))<0||fseek(f,0,SEEK_SET)!=0){fclose(f);return NULL;}
    p=(unsigned char*)malloc((size_t)n); if(!p){fclose(f);return NULL;}
    if(fread(p,1,(size_t)n,f)!=(size_t)n){free(p);fclose(f);return NULL;}
    fclose(f);*size=(size_t)n;return p;
}

int main(int argc,char **argv)
{
    unsigned char *rom; size_t size=0; Ai2V05Runtime rt; Ai2V07Ppu ppu; Ai2V08MachineDevices dev;
    Ai2V05Identity cur,next; unsigned long steps=0,max_steps=2000000; const Ai2V06Trap *t;
    if(argc!=2){fprintf(stderr,"usage: %s ROM.nes\n",argv[0]);return 2;}
    rom=load_file(argv[1],&size);
    if(!rom||size!=16u+AI2_PRG_ROM_BYTES+AI2_CHR_ROM_BYTES||memcmp(rom,"NES\x1A",4)!=0){free(rom);return 2;}
    ai2_v06_init(&rt,rom+16u,AI2_PRG_ROM_BYTES,rom+16u+AI2_PRG_ROM_BYTES,AI2_CHR_ROM_BYTES,0u);
    ai2_v07_ppu_init(&ppu,0u,0xFFu);
    ai2_v08_machine_init(&dev,&ppu);
    ai2_v08_set_controller_live(&dev.apu,0u,0u);
    ai2_v08_set_controller_live(&dev.apu,1u,0u);
    ai2_v08_bind(&rt,&dev);
    if(ai2_v06_reset(&rt,&cur)!=0){free(rom);return 3;}
    while(steps<max_steps&&!ai2_v06_has_trap(&rt)){
        if(ai2_v06_step(&rt,cur,&next)!=0)break;
        cur=next;steps++;
    }
    t=ai2_v06_get_trap(&rt);
    printf("{\"schema\":1,\"mode\":\"v08-static-cold-boot-no-input\",\"steps_completed\":%lu,\"max_steps\":%lu,\"current_bank\":%u,\"current_pc\":%u,\"cpu_cycles\":%llu,\"ppu_cycles\":%llu,\"apu_cycles\":%llu,\"ppu_scanline\":%d,\"ppu_dot\":%u,\"frame_generation\":%llu,\"controller_reads\":%llu,\"controller_latches\":%llu,\"apu_register_reads\":%llu,\"apu_register_writes\":%llu,\"oam_dma_count\":%llu,\"oam_dma_cycles\":%llu,\"dmc_dma_count\":%llu,\"dmc_dma_cycles\":%llu,\"dma_collisions\":%llu,\"pcm_total_samples\":%llu,\"pcm_dropped\":%llu,\"ppu_error\":%u,\"trap_code\":%u,\"trap_source_pc\":%u,\"trap_address\":%u}\n",
        steps,max_steps,(unsigned)cur.bank,(unsigned)cur.pc,(unsigned long long)rt.cpu_cycles,(unsigned long long)rt.ppu_cycles,(unsigned long long)rt.apu_cycles,
        (int)ppu.scanline,(unsigned)ppu.dot,(unsigned long long)ppu.frame_generation,(unsigned long long)dev.apu.controller_reads,
        (unsigned long long)dev.apu.controller_latches,(unsigned long long)dev.apu.register_reads,(unsigned long long)dev.apu.register_writes,
        (unsigned long long)dev.apu.oam_dma_count,(unsigned long long)dev.apu.oam_dma_cycles,(unsigned long long)dev.apu.dmc_dma_count,
        (unsigned long long)dev.apu.dmc_dma_cycles,(unsigned long long)dev.apu.dma_collision_count,(unsigned long long)dev.apu.pcm_total_samples,
        (unsigned long long)dev.apu.pcm_dropped,(unsigned)ppu.error,t?(unsigned)t->code:0u,t?(unsigned)t->source_pc:0u,t?(unsigned)t->address:0u);
    free(rom);
    if(ppu.error!=AI2_V07_PPU_OK)return 6;
    return t&&t->code!=AI2_V06_TRAP_NONE?5:0;
}
