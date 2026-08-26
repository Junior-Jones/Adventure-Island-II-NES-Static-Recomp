#include "src/runtime/ai2_v06_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *load_file(const char *path, size_t *size)
{
    FILE *f=fopen(path,"rb"); long n; unsigned char *p;
    if(!f) return NULL;
    if(fseek(f,0,SEEK_END)!=0 || (n=ftell(f))<0 || fseek(f,0,SEEK_SET)!=0){fclose(f);return NULL;}
    p=(unsigned char*)malloc((size_t)n); if(!p){fclose(f);return NULL;}
    if(fread(p,1,(size_t)n,f)!=(size_t)n){free(p);fclose(f);return NULL;}
    fclose(f); *size=(size_t)n; return p;
}

int main(int argc,char **argv)
{
    unsigned char *rom; size_t size=0; Ai2V05Runtime rt; Ai2V05Identity cur,next; unsigned long steps=0,max_steps=100000;
    const Ai2V06Trap *t;
    if(argc!=2){fprintf(stderr,"usage: %s ROM.nes\n",argv[0]);return 2;}
    rom=load_file(argv[1],&size);
    if(!rom || size!=16u+AI2_PRG_ROM_BYTES+AI2_CHR_ROM_BYTES || memcmp(rom,"NES\x1A",4)!=0){free(rom);return 2;}
    ai2_v06_init(&rt,rom+16u,AI2_PRG_ROM_BYTES,rom+16u+AI2_PRG_ROM_BYTES,AI2_CHR_ROM_BYTES,0u);
    if(ai2_v06_reset(&rt,&cur)!=0){free(rom);return 3;}
    while(steps<max_steps && !ai2_v06_has_trap(&rt)){
        if(ai2_v06_step(&rt,cur,&next)!=0) break;
        cur=next; steps++;
    }
    t=ai2_v06_get_trap(&rt);
    printf("{\"schema\":1,\"mode\":\"static-reset-frontier-no-input\",\"steps_completed\":%lu,\"current_bank\":%u,\"current_pc\":%u,\"cpu_cycles\":%llu,\"ppu_cycles\":%llu,\"apu_cycles\":%llu,\"trap_code\":%u,\"trap_source_pc\":%u,\"trap_address\":%u,\"trap_is_write\":%u,\"trap_value\":%u}\n",
        steps,(unsigned)cur.bank,(unsigned)cur.pc,(unsigned long long)rt.cpu_cycles,(unsigned long long)rt.ppu_cycles,(unsigned long long)rt.apu_cycles,
        t?(unsigned)t->code:0u,t?(unsigned)t->source_pc:0u,t?(unsigned)t->address:0u,t?(unsigned)t->is_write:0u,t?(unsigned)t->value:0u);
    free(rom);
    if(steps>=max_steps) return 4;
    if(!t || (t->code!=AI2_V06_TRAP_PPU_NOT_CONSTRUCTED && t->code!=AI2_V06_TRAP_APU_IO_NOT_CONSTRUCTED && t->code!=AI2_V06_TRAP_DMA_NOT_CONSTRUCTED)) return 5;
    return 0;
}
