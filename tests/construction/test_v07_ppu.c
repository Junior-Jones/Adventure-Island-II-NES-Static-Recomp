#include "src/ppu/ai2_v07_ppu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* PPU unit tests do not execute generated game instructions, but the V06 runtime
 * object references these symbols. Keep the unit binary independent of V05 shards. */
Ai2V05StepResult ai2_v05_dispatch(Ai2V05Runtime *rt, Ai2V05Identity current, Ai2V05Identity *next)
{ (void)rt; (void)current; (void)next; return AI2_V05_STEP_MISSING_IDENTITY; }
int ai2_v05_generated_identity_exists(uint8_t bank, uint16_t pc)
{ (void)bank; (void)pc; return 0; }

static unsigned long long checks;
#define CHECK(x) do { checks++; if(!(x)) { fprintf(stderr,"FAIL:%s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while(0)

static uint8_t prg[AI2_PRG_ROM_BYTES];
static uint8_t chr[AI2_CHR_ROM_BYTES];

static void mapper_known(Ai2Mmc3State *m)
{
    unsigned r;
    for(r=0;r<8;r++) {
        (void)ai2_mmc3_cpu_write(m,0x8000u,(uint8_t)r);
        (void)ai2_mmc3_cpu_write(m,0x8001u,(uint8_t)(r*4u));
    }
    (void)ai2_mmc3_cpu_write(m,0xA000u,0u);
    (void)ai2_mmc3_cpu_write(m,0xA001u,0u);
    (void)ai2_mmc3_cpu_write(m,0xC000u,0u);
    (void)ai2_mmc3_cpu_write(m,0xC001u,0u);
    (void)ai2_mmc3_cpu_write(m,0xE000u,0u);
}

static void init(Ai2V05Runtime *rt,Ai2V07Ppu *ppu)
{
    memset(prg,0,sizeof(prg));
    memset(chr,0,sizeof(chr));
    ai2_v06_init(rt,prg,sizeof(prg),chr,sizeof(chr),0u);
    mapper_known(&rt->mapper);
    ai2_v07_ppu_init(ppu,0u,0xFFu);
    ai2_v07_ppu_bind(rt,ppu);
}

static int test_memory(void)
{
    Ai2V05Runtime rt; Ai2V07Ppu p; uint8_t v=0;
    init(&rt,&p);
    /* R2 controls $1000-$13FF with CHR inversion clear; mapper_known sets R2=8. */
    chr[8u*1024u]=0xA5u;
    CHECK(ai2_v07_ppu_memory_read(&rt,&p,0x1000u,&v)==0 && v==0xA5u);
    CHECK(ai2_v07_ppu_memory_write(&rt,&p,0x1000u,0x5Au)==0);
    CHECK(chr[8u*1024u]==0xA5u);

    CHECK(ai2_v07_ppu_memory_write(&rt,&p,0x2000u,0x11u)==0);
    CHECK(ai2_v07_ppu_memory_read(&rt,&p,0x2800u,&v)==0 && v==0x11u); /* vertical */
    CHECK(ai2_mmc3_cpu_write(&rt.mapper,0xA000u,1u)==AI2_MAPPER_WRITE_APPLIED);
    CHECK(ai2_v07_ppu_memory_write(&rt,&p,0x2000u,0x22u)==0);
    CHECK(ai2_v07_ppu_memory_read(&rt,&p,0x2400u,&v)==0 && v==0x22u); /* horizontal */
    CHECK(ai2_v07_ppu_memory_write(&rt,&p,0x3F10u,0x7Fu)==0);
    CHECK(ai2_v07_ppu_memory_read(&rt,&p,0x3F00u,&v)==0 && v==0x3Fu);
    CHECK(p.palette[0]==0x3Fu && p.palette[0x10]==0u);
    CHECK(p.ppu_memory_reads>=4u && p.ppu_memory_writes>=4u);
    return 0;
}

static int test_registers(void)
{
    Ai2V05Runtime rt; Ai2V07Ppu p; uint8_t v;
    init(&rt,&p);
    CHECK(ai2_v07_ppu_cpu_write(&rt,&p,0x2000u,0x87u)==0);
    CHECK(p.ctrl==0x87u && (p.t&0x0C00u)==0x0C00u);
    CHECK(ai2_v07_ppu_cpu_write(&rt,&p,0x2005u,0x2Du)==0);
    CHECK(p.fine_x==5u && p.write_toggle==1u && (p.t&0x1Fu)==5u);
    CHECK(ai2_v07_ppu_cpu_write(&rt,&p,0x2005u,0x9Bu)==0);
    CHECK(p.write_toggle==0u && ((p.t>>12)&7u)==3u && ((p.t>>5)&0x1Fu)==19u);
    CHECK(ai2_v07_ppu_cpu_write(&rt,&p,0x2006u,0x21u)==0);
    CHECK(ai2_v07_ppu_cpu_write(&rt,&p,0x2006u,0x23u)==0);
    CHECK(p.v==0x2123u && p.t==0x2123u && p.write_toggle==0u);
    CHECK(ai2_v07_ppu_cpu_write(&rt,&p,0x2003u,0x02u)==0);
    CHECK(ai2_v07_ppu_cpu_write(&rt,&p,0x2004u,0xFFu)==0);
    CHECK(p.oam[2]==0xE3u && p.oam_addr==3u);

    /* Buffered PPUDATA reads and increment-by-32. */
    CHECK(ai2_mmc3_cpu_write(&rt.mapper,0xA000u,0u)==AI2_MAPPER_WRITE_APPLIED);
    p.ctrl=0x04u; p.v=0x2000u; p.read_buffer=0x55u; p.ciram[0]=0x66u; v=0xA0u;
    CHECK(ai2_v07_ppu_cpu_read(&rt,&p,0x2007u,&v)==0 && v==0x55u);
    CHECK(p.read_buffer==0x66u && p.v==0x2020u);

    /* Palette read is immediate, preserves upper open-bus bits, and fills buffer from $2Fxx. */
    p.v=0x3F00u; p.palette[0]=0x2Au; p.ciram[0x700u]=0x77u; v=0xC0u;
    CHECK(ai2_v07_ppu_cpu_read(&rt,&p,0x2007u,&v)==0 && v==0xEAu);
    CHECK(p.read_buffer==0x77u);

    p.status=(uint8_t)(AI2_V07_STATUS_VBLANK|AI2_V07_STATUS_SPRITE0|AI2_V07_STATUS_OVERFLOW);
    p.write_toggle=1u; v=0x1Bu;
    CHECK(ai2_v07_ppu_cpu_read(&rt,&p,0x2002u,&v)==0);
    CHECK(v==0xFBu && (p.status&AI2_V07_STATUS_VBLANK)==0u && p.write_toggle==0u);
    return 0;
}

static int test_nmi_and_race(void)
{
    Ai2V05Runtime rt; Ai2V07Ppu p; uint8_t v;
    init(&rt,&p);
    p.ctrl=0x80u; p.scanline=241; p.dot=0;
    ai2_v07_ppu_tick(&rt,&p); /* dot 0 -> 1 */
    CHECK((p.status&AI2_V07_STATUS_VBLANK)==0u && rt.nmi_pending==0u);
    ai2_v07_ppu_tick(&rt,&p); /* dot 1 */
    CHECK((p.status&AI2_V07_STATUS_VBLANK)!=0u && rt.nmi_pending==1u && p.nmi_line==1u);
    v=0;
    CHECK(ai2_v07_ppu_cpu_read(&rt,&p,0x2002u,&v)==0);
    CHECK((p.status&AI2_V07_STATUS_VBLANK)==0u && rt.nmi_pending==0u && p.nmi_line==0u);

    init(&rt,&p);
    p.ctrl=0x80u; p.scanline=241; p.dot=0; v=0;
    CHECK(ai2_v07_ppu_cpu_read(&rt,&p,0x2002u,&v)==0);
    CHECK(p.suppress_vblank==1u);
    ai2_v07_ppu_tick(&rt,&p); ai2_v07_ppu_tick(&rt,&p);
    CHECK((p.status&AI2_V07_STATUS_VBLANK)==0u && rt.nmi_pending==0u);

    init(&rt,&p);
    p.status=AI2_V07_STATUS_VBLANK; p.ctrl=0u;
    CHECK(ai2_v07_ppu_cpu_write(&rt,&p,0x2000u,0x80u)==0);
    CHECK(rt.nmi_pending==1u && p.nmi_line==1u);
    CHECK(ai2_v07_ppu_cpu_write(&rt,&p,0x2000u,0u)==0);
    CHECK(rt.nmi_pending==0u && p.nmi_line==0u);
    return 0;
}

static int test_scroll_helpers_via_ticks(void)
{
    Ai2V05Runtime rt; Ai2V07Ppu p;
    init(&rt,&p);
    p.mask=0x08u; p.scanline=0; p.dot=256; p.v=(uint16_t)(7u<<12 | 29u<<5 | 31u); p.t=0x0412u;
    ai2_v07_ppu_tick(&rt,&p);
    CHECK(((p.v>>12)&7u)==0u);
    CHECK(((p.v>>5)&0x1Fu)==0u);
    CHECK((p.v&0x0800u)!=0u);
    p.dot=257u;
    ai2_v07_ppu_tick(&rt,&p);
    CHECK((p.v&0x041Fu)==(p.t&0x041Fu));
    p.scanline=261; p.dot=280u; p.v=0; p.t=0x7BE0u;
    ai2_v07_ppu_tick(&rt,&p);
    CHECK((p.v&0x7BE0u)==0x7BE0u);
    return 0;
}

static int test_pixels_and_sprites(void)
{
    Ai2V05Runtime rt; Ai2V07Ppu p;
    init(&rt,&p);
    p.mask=0x1Eu; p.scanline=10; p.dot=1u; p.fine_x=0u;
    p.bg_shift_lo=0x8000u; p.bg_shift_hi=0u; p.at_shift_lo=0u; p.at_shift_hi=0u;
    p.palette[1]=0x11u; p.palette[0x11]=0x22u;
    p.sprite_count=1u; p.sprites[0].valid=1u; p.sprites[0].x=0u; p.sprites[0].pattern_lo=0x80u;
    p.sprites[0].pattern_hi=0u; p.sprites[0].attr=0u; p.sprites[0].oam_index=0u;
    ai2_v07_ppu_tick(&rt,&p);
    CHECK((p.status&AI2_V07_STATUS_SPRITE0)!=0u);
    CHECK((p.frame_indexed[10u*256u]&0x3Fu)==0x22u);

    init(&rt,&p);
    p.mask=0x1Eu; p.scanline=10; p.dot=1u; p.bg_shift_lo=0x8000u; p.palette[1]=0x11u; p.palette[0x11]=0x22u;
    p.sprite_count=1u; p.sprites[0].valid=1u; p.sprites[0].pattern_lo=0x80u; p.sprites[0].attr=0x20u; p.sprites[0].oam_index=0u;
    ai2_v07_ppu_tick(&rt,&p);
    CHECK((p.frame_indexed[10u*256u]&0x3Fu)==0x11u);

    init(&rt,&p);
    p.mask=0x18u; p.scanline=5; p.dot=257u;
    for(unsigned i=0;i<9u;i++) { p.oam[i*4u]=5u; p.oam[i*4u+1u]=(uint8_t)i; p.oam[i*4u+3u]=(uint8_t)i; }
    ai2_v07_ppu_tick(&rt,&p);
    CHECK(p.next_sprite_count==8u && (p.status&AI2_V07_STATUS_OVERFLOW)!=0u);
    return 0;
}

static int test_target_8x16_sprite_fetch(void)
{
    Ai2V05Runtime rt; Ai2V07Ppu p;
    init(&rt,&p);
    p.ctrl=0x20u; /* target-used 8x16 sprite mode */
    p.mask=0x10u; p.scanline=0; p.dot=257u;
    for(unsigned i=0;i<64u;i++) p.oam[i*4u]=0x80u;
    p.oam[0]=0u; p.oam[1]=3u; p.oam[2]=0u; p.oam[3]=0u;
    /* Odd tile index selects $1000 table, even tile pair begins at tile 2. */
    chr[8u*1024u + 0x20u]=0x81u;
    ai2_v07_ppu_tick(&rt,&p); /* eval + sprite slot phase 0 */
    CHECK(p.next_sprite_count==1u && p.next_sprites[0].row==0u);
    while(p.dot<262u) ai2_v07_ppu_tick(&rt,&p);
    CHECK(p.next_sprites[0].pattern_lo==0x81u);

    init(&rt,&p);
    p.ctrl=0x20u; p.mask=0x10u; p.scanline=0; p.dot=257u;
    for(unsigned i=0;i<64u;i++) p.oam[i*4u]=0x80u;
    p.oam[0]=0u; p.oam[1]=3u; p.oam[2]=0x40u; p.oam[3]=0u;
    chr[8u*1024u + 0x20u]=0x81u;
    ai2_v07_ppu_tick(&rt,&p);
    while(p.dot<262u) ai2_v07_ppu_tick(&rt,&p);
    CHECK(p.next_sprites[0].pattern_lo==0x81u); /* 0x81 is symmetric under bit reverse */
    return 0;
}

static int test_a12(void)
{
    Ai2V05Runtime rt; Ai2V07Ppu p; uint8_t v;
    init(&rt,&p);
    CHECK(ai2_mmc3_cpu_write(&rt.mapper,0xC000u,0u)==AI2_MAPPER_WRITE_APPLIED);
    CHECK(ai2_mmc3_cpu_write(&rt.mapper,0xC001u,0u)==AI2_MAPPER_WRITE_APPLIED);
    CHECK(ai2_mmc3_cpu_write(&rt.mapper,0xE001u,0u)==AI2_MAPPER_WRITE_APPLIED);
    CHECK(ai2_v07_ppu_memory_read(&rt,&p,0x0000u,&v)==0);
    ai2_mmc3_clock_m2_falling_edge(&rt.mapper);
    ai2_mmc3_clock_m2_falling_edge(&rt.mapper);
    ai2_mmc3_clock_m2_falling_edge(&rt.mapper);
    CHECK(ai2_v07_ppu_memory_read(&rt,&p,0x1000u,&v)==0);
    CHECK(rt.mapper.irq_pending_known && rt.mapper.irq_pending);
    CHECK(p.mapper_a12_clocks==1u);
    return 0;
}

static int test_frame_timing_and_copy(void)
{
    Ai2V05Runtime rt; Ai2V07Ppu p; uint64_t ticks=0, first=0, second=0, gen=0; uint16_t *idx; uint32_t *bgra;
    init(&rt,&p);
    p.mask=0x08u; p.scanline=0; p.dot=0; p.frame_count=0; p.frame_generation=0;
    while(!(p.scanline==0 && p.dot==0 && ticks>0)) { ai2_v07_ppu_tick(&rt,&p); CHECK(!ai2_v06_has_trap(&rt)); ticks++; CHECK(ticks<90000u); }
    first=ticks; ticks=0;
    while(!(p.scanline==0 && p.dot==0 && ticks>0)) { ai2_v07_ppu_tick(&rt,&p); CHECK(!ai2_v06_has_trap(&rt)); ticks++; CHECK(ticks<90000u); }
    second=ticks;
    CHECK(first==89341u && second==89342u);
    CHECK(p.frame_generation>=2u && p.frame_count>=2u);
    idx=(uint16_t*)malloc(sizeof(uint16_t)*AI2_V07_SCREEN_PIXELS);
    bgra=(uint32_t*)malloc(sizeof(uint32_t)*AI2_V07_SCREEN_PIXELS);
    CHECK(idx!=NULL && bgra!=NULL);
    CHECK(ai2_v07_ppu_copy_indexed(&p,idx,AI2_V07_SCREEN_PIXELS,&gen)==0 && gen==p.frame_generation);
    CHECK(ai2_v07_ppu_copy_bgra(&p,bgra,AI2_V07_SCREEN_PIXELS,&gen)==0 && (bgra[0]&0xFF000000u)==0xFF000000u);
    CHECK(ai2_v07_ppu_copy_indexed(&p,idx,AI2_V07_SCREEN_PIXELS-1u,NULL)!=0);
    free(idx); free(bgra);
    return 0;
}

int main(void)
{
    if(test_memory()!=0) return 1;
    if(test_registers()!=0) return 1;
    if(test_nmi_and_race()!=0) return 1;
    if(test_scroll_helpers_via_ticks()!=0) return 1;
    if(test_pixels_and_sprites()!=0) return 1;
    if(test_target_8x16_sprite_fetch()!=0) return 1;
    if(test_a12()!=0) return 1;
    if(test_frame_timing_and_copy()!=0) return 1;
    printf("V07 PPU construction tests: PASS (%llu checks)\n",checks);
    return 0;
}
