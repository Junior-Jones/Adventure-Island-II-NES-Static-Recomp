#include "src/apu/ai2_v08_apu.h"
#include <stdio.h>
#include <string.h>

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
    for(r=0;r<8u;r++) {
        (void)ai2_mmc3_cpu_write(m,0x8000u,(uint8_t)r);
        (void)ai2_mmc3_cpu_write(m,0x8001u,(uint8_t)(r*4u));
    }
    (void)ai2_mmc3_cpu_write(m,0xA000u,0u);
    (void)ai2_mmc3_cpu_write(m,0xA001u,0u);
    (void)ai2_mmc3_cpu_write(m,0xC000u,0u);
    (void)ai2_mmc3_cpu_write(m,0xC001u,0u);
    (void)ai2_mmc3_cpu_write(m,0xE000u,0u);
}

static void init(Ai2V05Runtime *rt,Ai2V07Ppu *ppu,Ai2V08MachineDevices *dev)
{
    memset(prg,0,sizeof(prg)); memset(chr,0,sizeof(chr));
    ai2_v06_init(rt,prg,sizeof(prg),chr,sizeof(chr),0u);
    mapper_known(&rt->mapper);
    ai2_v07_ppu_init(ppu,0u,0u);
    ai2_v08_machine_init(dev,ppu);
    ai2_v08_bind(rt,dev);
}

static int test_controller(void)
{
    Ai2V05Runtime rt; Ai2V07Ppu p; Ai2V08MachineDevices d; uint8_t v=0; unsigned i;
    init(&rt,&p,&d);
    ai2_v08_set_controller_live(&d.apu,0,AI2_V08_BTN_A|AI2_V08_BTN_START|AI2_V08_BTN_RIGHT);
    ai2_v08_set_controller_live(&d.apu,1,AI2_V08_BTN_B|AI2_V08_BTN_LEFT);
    CHECK(ai2_v06_bus_write(&rt,0x4016u,1u,AI2_V06_BUS_WRITE)==0);
    CHECK(ai2_v06_bus_write(&rt,0x4016u,0u,AI2_V06_BUS_WRITE)==0);
    CHECK(d.apu.controller_latches==2u);
    { const uint8_t bits[8]={1,0,0,1,0,0,0,1};
      for(i=0;i<8u;i++){ CHECK(ai2_v06_bus_read(&rt,0x4016u,AI2_V06_BUS_READ,&v)==0); CHECK((v&1u)==bits[i]); } }
    CHECK(ai2_v06_bus_read(&rt,0x4016u,AI2_V06_BUS_READ,&v)==0 && (v&1u)==1u);
    /* Host changes after the latch do not alter the serial snapshot. */
    ai2_v08_set_controller_live(&d.apu,1,0u);
    { const uint8_t bits2[8]={0,1,0,0,0,0,1,0};
      for(i=0;i<8u;i++){ CHECK(ai2_v06_bus_read(&rt,0x4017u,AI2_V06_BUS_READ,&v)==0); CHECK((v&1u)==bits2[i]); } }
    CHECK(d.apu.controller_reads==17u);
    return 0;
}

static int test_channels_and_status(void)
{
    Ai2V05Runtime rt; Ai2V07Ppu p; Ai2V08MachineDevices d; uint8_t v=0; unsigned i;
    init(&rt,&p,&d);
    CHECK(ai2_v06_bus_write(&rt,0x4015u,0x0Fu,AI2_V06_BUS_WRITE)==0);
    CHECK(ai2_v06_bus_write(&rt,0x4000u,0xDFu,AI2_V06_BUS_WRITE)==0);
    CHECK(ai2_v06_bus_write(&rt,0x4002u,0x08u,AI2_V06_BUS_WRITE)==0);
    CHECK(ai2_v06_bus_write(&rt,0x4003u,0x08u,AI2_V06_BUS_WRITE)==0);
    CHECK(d.apu.pulse1.envelope.length.counter!=0u);
    CHECK(ai2_v06_bus_write(&rt,0x4004u,0x9Au,AI2_V06_BUS_WRITE)==0);
    CHECK(ai2_v06_bus_write(&rt,0x4006u,0x10u,AI2_V06_BUS_WRITE)==0);
    CHECK(ai2_v06_bus_write(&rt,0x4007u,0x08u,AI2_V06_BUS_WRITE)==0);
    CHECK(ai2_v06_bus_write(&rt,0x4008u,0x85u,AI2_V06_BUS_WRITE)==0);
    CHECK(ai2_v06_bus_write(&rt,0x400Au,0x02u,AI2_V06_BUS_WRITE)==0);
    CHECK(ai2_v06_bus_write(&rt,0x400Bu,0x08u,AI2_V06_BUS_WRITE)==0);
    CHECK(ai2_v06_bus_write(&rt,0x400Cu,0x1Fu,AI2_V06_BUS_WRITE)==0);
    CHECK(ai2_v06_bus_write(&rt,0x400Eu,0x8Fu,AI2_V06_BUS_WRITE)==0);
    CHECK(ai2_v06_bus_write(&rt,0x400Fu,0x08u,AI2_V06_BUS_WRITE)==0);
    CHECK(d.apu.triangle.length.counter!=0u && d.apu.noise.envelope.length.counter!=0u);
    CHECK(ai2_v06_bus_read(&rt,0x4015u,AI2_V06_BUS_READ,&v)==0);
    CHECK((v&0x0Fu)==0x0Fu);
    for(i=0;i<8000u;i++) ai2_v08_apu_tick(&rt,&d);
    CHECK(d.apu.pulse1.duty_pos!=0u);
    CHECK(d.apu.triangle.sequence_pos!=0u);
    CHECK(d.apu.noise.lfsr!=1u);
    CHECK(ai2_v08_current_mix(&d.apu)>=0);
    CHECK(ai2_v06_bus_write(&rt,0x4015u,0u,AI2_V06_BUS_WRITE)==0);
    CHECK(d.apu.pulse1.envelope.length.counter==0u && d.apu.triangle.length.counter==0u);
    return 0;
}

static int test_frame_irq(void)
{
    Ai2V05Runtime rt; Ai2V07Ppu p; Ai2V08MachineDevices d; uint8_t v=0; unsigned i;
    init(&rt,&p,&d); d.apu.frame_write_delay=-1; d.apu.frame_cycle=0;
    for(i=0;i<29829u;i++) ai2_v08_apu_tick(&rt,&d);
    CHECK(d.apu.frame_irq_flag==1u && rt.external_irq==1u);
    CHECK(d.apu.quarter_frame_ticks>=4u && d.apu.half_frame_ticks>=2u);
    rt.open_bus=0x20u;
    CHECK(ai2_v08_apu_io_read(&rt,&d,0x4015u,&v)==0);
    CHECK((v&0x40u)!=0u && (v&0x20u)!=0u);
    CHECK(d.apu.frame_irq_flag==0u && rt.external_irq==0u);
    rt.cpu_cycles=2u;
    CHECK(ai2_v08_apu_io_write(&rt,&d,0x4017u,0xC0u)==0);
    CHECK(d.apu.frame_write_delay==3 && d.apu.frame_irq_inhibit==1u);
    ai2_v08_apu_tick(&rt,&d); ai2_v08_apu_tick(&rt,&d); CHECK(d.apu.frame_mode5==0u);
    ai2_v08_apu_tick(&rt,&d); CHECK(d.apu.frame_mode5==1u && d.apu.frame_cycle==0u);
    return 0;
}

static int test_dmc(void)
{
    Ai2V05Runtime rt; Ai2V07Ppu p; Ai2V08MachineDevices d; Ai2MapResult m;
    init(&rt,&p,&d);
    m=ai2_mmc3_map_cpu_read(&rt.mapper,0xC000u); CHECK(m.kind==AI2_MAP_PRG_ROM); prg[m.segment_offset]=0xA5u;
    d.apu.dmc.enabled=1u; d.apu.dmc.irq_enabled=1u; d.apu.dmc.current_address=0xC000u;
    d.apu.dmc.bytes_remaining=1u; d.apu.dmc.sample_buffer_empty=1u; d.apu.dmc.dma_pending=1u;
    { uint64_t before=rt.cpu_cycles; rt.dma_draining=1u; ai2_v08_dma_drain(&rt,&d); rt.dma_draining=0u; CHECK(rt.cpu_cycles-before==4u); }
    CHECK(d.apu.dmc_dma_count==1u && d.apu.dmc_dma_cycles==4u);
    CHECK(d.apu.dmc.sample_buffer==0xA5u && d.apu.dmc.sample_buffer_empty==0u);
    CHECK(d.apu.dmc.bytes_remaining==0u && d.apu.dmc.irq_flag==1u && rt.external_irq==1u);
    d.apu.dmc.current_address=0xFFFFu; d.apu.dmc.bytes_remaining=1u; d.apu.dmc.dma_pending=1u; d.apu.dmc.sample_buffer_empty=1u; d.apu.dmc.irq_enabled=0u;
    rt.dma_draining=1u; ai2_v08_dma_drain(&rt,&d); rt.dma_draining=0u; CHECK(d.apu.dmc.current_address==0x8000u);
    return 0;
}

static int test_oam_dma(void)
{
    Ai2V05Runtime rt; Ai2V07Ppu p; Ai2V08MachineDevices d; unsigned i; uint64_t before, delta;
    init(&rt,&p,&d); for(i=0;i<256u;i++) rt.ram[0x200u+i]=(uint8_t)i;
    p.oam_addr=0u; before=rt.cpu_cycles;
    CHECK(ai2_v06_bus_write(&rt,0x4014u,0x02u,AI2_V06_BUS_WRITE)==0);
    delta=rt.cpu_cycles-before;
    CHECK(delta==514u || delta==515u); /* 1 write + 513/514 stolen cycles */
    CHECK(d.apu.oam_dma_count==1u && (d.apu.oam_dma_cycles==513u || d.apu.oam_dma_cycles==514u));
    for(i=0;i<256u;i++) { uint8_t e=(uint8_t)i; if((i&3u)==2u) e&=0xE3u; CHECK(p.oam[i]==e); }
    CHECK(rt.ppu_cycles==rt.cpu_cycles*3u && rt.apu_cycles==rt.cpu_cycles);

    init(&rt,&p,&d); for(i=0;i<256u;i++) rt.ram[0x300u+i]=(uint8_t)(255u-i);
    { Ai2MapResult m=ai2_mmc3_map_cpu_read(&rt.mapper,0xC000u); CHECK(m.kind==AI2_MAP_PRG_ROM); prg[m.segment_offset]=0x5Au; }
    d.apu.dmc.enabled=1u; d.apu.dmc.current_address=0xC000u; d.apu.dmc.bytes_remaining=1u; d.apu.dmc.sample_buffer_empty=1u; d.apu.dmc.dma_pending=1u;
    CHECK(ai2_v06_bus_write(&rt,0x4014u,0x03u,AI2_V06_BUS_WRITE)==0);
    CHECK(d.apu.dma_collision_count>=1u && d.apu.dmc_dma_count==1u);
    CHECK(p.oam[0]==255u && p.oam[255]==0u);
    return 0;
}

static int test_pcm_and_mix(void)
{
    Ai2V05Runtime rt; Ai2V07Ppu p; Ai2V08MachineDevices d; int16_t out[32]; size_t n; unsigned i;
    init(&rt,&p,&d);
    d.apu.pulse1.output=15u; d.apu.pulse2.output=15u; d.apu.triangle.output=15u; d.apu.noise.output=15u; d.apu.dmc.output_level=127u;
    CHECK(ai2_v08_current_mix(&d.apu)>0);
    for(i=0;i<400000u;i++) ai2_v08_apu_tick(&rt,&d);
    CHECK(d.apu.pcm_count==AI2_V08_PCM_CAPACITY && d.apu.pcm_dropped>0u);
    CHECK(d.apu.pcm_total_samples>AI2_V08_PCM_CAPACITY);
    n=ai2_v08_copy_pcm(&d.apu,out,32u); CHECK(n==32u && d.apu.pcm_count==AI2_V08_PCM_CAPACITY-32u);
    return 0;
}


static int test_exhaustive_register_tables(void)
{
    static const uint8_t lengths[32]={10,254,20,2,40,4,80,6,160,8,60,10,14,12,26,14,12,16,24,18,48,20,96,22,192,24,72,26,16,28,32,30};
    static const uint16_t noise_periods[16]={4,8,16,32,64,96,128,160,202,254,380,508,762,1016,2034,4068};
    static const uint16_t dmc_periods[16]={428,380,340,320,286,254,226,214,190,160,142,128,106,85,72,54};
    Ai2V05Runtime rt; Ai2V07Ppu ppu; Ai2V08MachineDevices d; unsigned v,i; uint8_t got;
    for(v=0;v<256u;v++) {
        init(&rt,&ppu,&d);
        CHECK(ai2_v08_apu_io_write(&rt,&d,0x4010u,(uint8_t)v)==0);
        CHECK(d.apu.dmc.period==dmc_periods[v&15u]);
        CHECK(d.apu.dmc.irq_enabled==((v>>7)&1u));
        CHECK(d.apu.dmc.loop==((v>>6)&1u));
        CHECK(ai2_v08_apu_io_write(&rt,&d,0x4011u,(uint8_t)v)==0);
        CHECK(d.apu.dmc.output_level==(v&0x7Fu));
        CHECK(ai2_v08_apu_io_write(&rt,&d,0x4012u,(uint8_t)v)==0);
        CHECK(d.apu.dmc.sample_address==(uint16_t)(0xC000u|((uint16_t)v<<6)));
        CHECK(ai2_v08_apu_io_write(&rt,&d,0x4013u,(uint8_t)v)==0);
        CHECK(d.apu.dmc.sample_length==(uint16_t)(((uint16_t)v<<4)|1u));
        CHECK(ai2_v08_apu_io_write(&rt,&d,0x400Eu,(uint8_t)v)==0);
        CHECK(d.apu.noise.timer_period==noise_periods[v&15u]);
        CHECK(d.apu.noise.mode==((v>>7)&1u));
    }
    for(i=0;i<32u;i++) {
        init(&rt,&ppu,&d);
        CHECK(ai2_v08_apu_io_write(&rt,&d,0x4015u,0x0Fu)==0);
        CHECK(ai2_v08_apu_io_write(&rt,&d,0x4003u,(uint8_t)(i<<3))==0);
        CHECK(d.apu.pulse1.envelope.length.counter==lengths[i]);
        CHECK(ai2_v08_apu_io_write(&rt,&d,0x4007u,(uint8_t)(i<<3))==0);
        CHECK(d.apu.pulse2.envelope.length.counter==lengths[i]);
        CHECK(ai2_v08_apu_io_write(&rt,&d,0x400Bu,(uint8_t)(i<<3))==0);
        CHECK(d.apu.triangle.length.counter==lengths[i]);
        CHECK(ai2_v08_apu_io_write(&rt,&d,0x400Fu,(uint8_t)(i<<3))==0);
        CHECK(d.apu.noise.envelope.length.counter==lengths[i]);
    }
    for(v=0;v<256u;v++) {
        init(&rt,&ppu,&d);
        ai2_v08_set_controller_live(&d.apu,0u,(uint8_t)v);
        CHECK(ai2_v08_apu_io_write(&rt,&d,0x4016u,1u)==0);
        CHECK(ai2_v08_apu_io_write(&rt,&d,0x4016u,0u)==0);
        for(i=0;i<8u;i++) {
            got=0u; CHECK(ai2_v08_apu_io_read(&rt,&d,0x4016u,&got)==0);
            CHECK((got&1u)==((v>>i)&1u));
        }
    }
    return 0;
}

static int test_dma_parity_and_dmc_loop(void)
{
    Ai2V05Runtime rt; Ai2V07Ppu p; Ai2V08MachineDevices d; Ai2MapResult m; uint64_t before; unsigned i;
    init(&rt,&p,&d); for(i=0;i<256u;i++) rt.ram[0x400u+i]=(uint8_t)i;
    before=rt.cpu_cycles; CHECK(ai2_v06_bus_write(&rt,0x4014u,0x04u,AI2_V06_BUS_WRITE)==0);
    CHECK(rt.cpu_cycles-before==515u && d.apu.oam_dma_cycles==514u);
    init(&rt,&p,&d); for(i=0;i<256u;i++) rt.ram[0x400u+i]=(uint8_t)i;
    ai2_v06_internal_cycle(&rt); before=rt.cpu_cycles;
    CHECK(ai2_v06_bus_write(&rt,0x4014u,0x04u,AI2_V06_BUS_WRITE)==0);
    CHECK(rt.cpu_cycles-before==514u && d.apu.oam_dma_cycles==513u);

    init(&rt,&p,&d); m=ai2_mmc3_map_cpu_read(&rt.mapper,0xC000u); CHECK(m.kind==AI2_MAP_PRG_ROM); prg[m.segment_offset]=0x5Au;
    d.apu.dmc.enabled=1u; d.apu.dmc.loop=1u; d.apu.dmc.irq_enabled=1u; d.apu.dmc.sample_address=0xC000u; d.apu.dmc.sample_length=3u;
    d.apu.dmc.current_address=0xC000u; d.apu.dmc.bytes_remaining=1u; d.apu.dmc.sample_buffer_empty=1u; d.apu.dmc.dma_pending=1u;
    rt.dma_draining=1u; ai2_v08_dma_drain(&rt,&d); rt.dma_draining=0u;
    CHECK(d.apu.dmc.bytes_remaining==3u && d.apu.dmc.current_address==0xC000u && d.apu.dmc.irq_flag==0u);
    return 0;
}

int main(void)
{
    if(test_controller()||test_channels_and_status()||test_frame_irq()||test_dmc()||test_oam_dma()||test_pcm_and_mix()||test_exhaustive_register_tables()||test_dma_parity_and_dmc_loop()) return 1;
    printf("V08 APU/DMA/input construction: PASS (%llu checks)\n",checks);
    return 0;
}
