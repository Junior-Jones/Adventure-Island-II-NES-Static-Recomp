#include "src/runtime/ai2_v06_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long long checks = 0;
static unsigned failures = 0;

#define CHECK(cond) do { checks++; if(!(cond)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; \
} } while(0)

typedef struct EventLog {
    Ai2V06BusEvent events[64];
    size_t count;
} EventLog;

typedef struct DeviceState {
    unsigned ppu_ticks;
    unsigned apu_ticks;
    unsigned dma_ticks;
    unsigned ppu_reads;
    unsigned ppu_writes;
    unsigned apu_reads;
    unsigned apu_writes;
    unsigned dma_starts;
    uint16_t last_address;
    uint8_t last_value;
} DeviceState;

static void observe(void *user, const Ai2V06BusEvent *event)
{
    EventLog *log = (EventLog *)user;
    if(log->count < sizeof(log->events) / sizeof(log->events[0])) {
        log->events[log->count++] = *event;
    }
}

static int dev_ppu_read(Ai2V05Runtime *rt, void *user, uint16_t address, uint8_t *value)
{
    DeviceState *d = (DeviceState *)user;
    (void)rt;
    d->ppu_reads++;
    d->last_address = address;
    *value = (uint8_t)(0x80u | (address & 7u));
    return 0;
}

static int dev_ppu_write(Ai2V05Runtime *rt, void *user, uint16_t address, uint8_t value)
{
    DeviceState *d = (DeviceState *)user;
    (void)rt;
    d->ppu_writes++;
    d->last_address = address;
    d->last_value = value;
    return 0;
}

static int dev_apu_read(Ai2V05Runtime *rt, void *user, uint16_t address, uint8_t *value)
{
    DeviceState *d = (DeviceState *)user;
    (void)rt;
    d->apu_reads++;
    d->last_address = address;
    *value = 0x5Au;
    return 0;
}

static int dev_apu_write(Ai2V05Runtime *rt, void *user, uint16_t address, uint8_t value)
{
    DeviceState *d = (DeviceState *)user;
    (void)rt;
    d->apu_writes++;
    d->last_address = address;
    d->last_value = value;
    return 0;
}

static int dev_dma_start(Ai2V05Runtime *rt, void *user, uint16_t address, uint8_t value)
{
    DeviceState *d = (DeviceState *)user;
    (void)rt;
    d->dma_starts++;
    d->last_address = address;
    d->last_value = value;
    return 0;
}

static void dev_ppu_tick(Ai2V05Runtime *rt, void *user) { (void)rt; ((DeviceState *)user)->ppu_ticks++; }
static void dev_apu_tick(Ai2V05Runtime *rt, void *user) { (void)rt; ((DeviceState *)user)->apu_ticks++; }
static void dev_dma_tick(Ai2V05Runtime *rt, void *user) { (void)rt; ((DeviceState *)user)->dma_ticks++; }

static unsigned char *load_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    long n;
    unsigned char *data;
    if(f == NULL) return NULL;
    if(fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    n = ftell(f);
    if(n < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    data = (unsigned char *)malloc((size_t)n);
    if(data == NULL) { fclose(f); return NULL; }
    if(fread(data, 1, (size_t)n, f) != (size_t)n) { free(data); fclose(f); return NULL; }
    fclose(f);
    *size = (size_t)n;
    return data;
}

static void init_rt(Ai2V05Runtime *rt, const uint8_t *prg, const uint8_t *chr)
{
    ai2_v06_init(rt, prg, AI2_PRG_ROM_BYTES, chr, AI2_CHR_ROM_BYTES, 0u);
    CHECK(!ai2_v06_has_trap(rt));
}

static void test_reset_and_bus(const uint8_t *prg, const uint8_t *chr)
{
    Ai2V05Runtime rt;
    Ai2V05Identity entry;
    uint8_t v = 0u;
    DeviceState d;
    Ai2V06DeviceHooks hooks;

    init_rt(&rt, prg, chr);
    CHECK(ai2_v06_reset(&rt, &entry) == 0);
    CHECK(entry.bank == 15u);
    CHECK(entry.pc == 0xFFAEu);
    CHECK(rt.current_pc == 0xFFAEu);
    CHECK(rt.cpu_cycles == 7u);
    CHECK(rt.ppu_cycles == 21u);
    CHECK(rt.apu_cycles == 7u);
    CHECK(rt.s == 0xFDu);
    CHECK(rt.p == (AI2_V06_FLAG_I | AI2_V06_FLAG_U));
    CHECK(!ai2_v06_has_trap(&rt));

    init_rt(&rt, prg, chr);
    CHECK(ai2_v06_bus_write(&rt, 0x0001u, 0xA5u, AI2_V06_BUS_WRITE) == 0);
    CHECK(ai2_v06_bus_read(&rt, 0x0801u, AI2_V06_BUS_READ, &v) == 0);
    CHECK(v == 0xA5u);
    CHECK(rt.ram[1] == 0xA5u);
    CHECK(rt.cpu_cycles == 2u && rt.ppu_cycles == 6u && rt.apu_cycles == 2u);
    CHECK(ai2_v06_bus_write(&rt, 0x4018u, 0x3Cu, AI2_V06_BUS_WRITE) == 0);
    CHECK(ai2_v06_bus_read(&rt, 0x401Fu, AI2_V06_BUS_READ, &v) == 0);
    CHECK(v == 0x3Cu);

    init_rt(&rt, prg, chr);
    rt.current_pc = 0xE010u;
    CHECK(ai2_v06_bus_read(&rt, 0x2000u, AI2_V06_BUS_READ, &v) != 0);
    CHECK(ai2_v06_get_trap(&rt)->code == AI2_V06_TRAP_PPU_NOT_CONSTRUCTED);
    CHECK(ai2_v06_get_trap(&rt)->address == 0x2000u);
    CHECK(rt.cpu_cycles == 0u);

    init_rt(&rt, prg, chr);
    rt.current_pc = 0xE010u;
    CHECK(ai2_v06_bus_read(&rt, 0x4000u, AI2_V06_BUS_READ, &v) != 0);
    CHECK(ai2_v06_get_trap(&rt)->code == AI2_V06_TRAP_APU_IO_NOT_CONSTRUCTED);
    CHECK(rt.cpu_cycles == 0u);

    init_rt(&rt, prg, chr);
    rt.current_pc = 0xE010u;
    CHECK(ai2_v06_bus_write(&rt, 0x4014u, 0x02u, AI2_V06_BUS_WRITE) != 0);
    CHECK(ai2_v06_get_trap(&rt)->code == AI2_V06_TRAP_DMA_NOT_CONSTRUCTED);
    CHECK(rt.cpu_cycles == 0u);

    memset(&d, 0, sizeof(d));
    memset(&hooks, 0, sizeof(hooks));
    hooks.user = &d;
    hooks.ppu_read = dev_ppu_read;
    hooks.ppu_write = dev_ppu_write;
    hooks.apu_io_read = dev_apu_read;
    hooks.apu_io_write = dev_apu_write;
    hooks.oam_dma_start = dev_dma_start;
    hooks.ppu_tick = dev_ppu_tick;
    hooks.apu_tick = dev_apu_tick;
    hooks.dma_tick = dev_dma_tick;
    init_rt(&rt, prg, chr);
    ai2_v06_set_devices(&rt, &hooks);
    CHECK(ai2_v06_bus_read(&rt, 0x3FF8u, AI2_V06_BUS_READ, &v) == 0);
    CHECK(d.ppu_reads == 1u && d.last_address == 0x2000u && v == 0x80u);
    CHECK(ai2_v06_bus_write(&rt, 0x2008u, 0x77u, AI2_V06_BUS_WRITE) == 0);
    CHECK(d.ppu_writes == 1u && d.last_address == 0x2000u && d.last_value == 0x77u);
    CHECK(ai2_v06_bus_read(&rt, 0x4016u, AI2_V06_BUS_READ, &v) == 0);
    CHECK(d.apu_reads == 1u && d.last_address == 0x4016u && v == 0x5Au);
    CHECK(ai2_v06_bus_write(&rt, 0x4015u, 0x19u, AI2_V06_BUS_WRITE) == 0);
    CHECK(d.apu_writes == 1u && d.last_address == 0x4015u && d.last_value == 0x19u);
    CHECK(ai2_v06_bus_write(&rt, 0x4014u, 0x03u, AI2_V06_BUS_WRITE) == 0);
    CHECK(d.dma_starts == 1u && d.last_address == 0x4014u && d.last_value == 0x03u);
    CHECK(rt.cpu_cycles == 5u && rt.apu_cycles == 5u && rt.ppu_cycles == 15u);
    CHECK(d.apu_ticks == 5u && d.dma_ticks == 5u && d.ppu_ticks == 15u);
}

static void test_mapper_shared_authority(const uint8_t *prg, const uint8_t *chr)
{
    Ai2V05Runtime rt;
    uint8_t bank = 0u, v = 0u;
    init_rt(&rt, prg, chr);
    CHECK(ai2_v05_runtime_map_prg_identity(&rt, 0x8000u, &bank) != 0);
    CHECK(ai2_v06_get_trap(&rt)->code == AI2_V06_TRAP_UNRESOLVED_MAPPER);

    init_rt(&rt, prg, chr);
    CHECK(ai2_v06_bus_write(&rt, 0x8000u, 0x06u, AI2_V06_BUS_WRITE) == 0);
    CHECK(ai2_v06_bus_write(&rt, 0x8001u, 0x03u, AI2_V06_BUS_WRITE) == 0);
    CHECK(ai2_v05_runtime_map_prg_identity(&rt, 0x8000u, &bank) == 0);
    CHECK(bank == 3u);
    CHECK(ai2_v06_bus_read(&rt, 0x8000u, AI2_V06_BUS_READ, &v) == 0);
    CHECK(v == prg[3u * AI2_PRG_BANK_BYTES]);

    CHECK(ai2_v06_bus_write(&rt, 0x8000u, 0x07u, AI2_V06_BUS_WRITE) == 0);
    CHECK(ai2_v06_bus_write(&rt, 0x8001u, 0x0Cu, AI2_V06_BUS_WRITE) == 0);
    CHECK(ai2_v05_runtime_map_prg_identity(&rt, 0xA000u, &bank) == 0);
    CHECK(bank == 12u);
    CHECK(ai2_v06_bus_read(&rt, 0xA006u, AI2_V06_BUS_READ, &v) == 0);
    CHECK(v == prg[12u * AI2_PRG_BANK_BYTES + 6u]);

    CHECK(ai2_v05_runtime_map_prg_identity(&rt, 0xE000u, &bank) == 0);
    CHECK(bank == 15u);
}

static void test_bus_order_and_addressing(const uint8_t *prg, const uint8_t *chr)
{
    Ai2V05Runtime rt;
    Ai2V05SemanticResult r;
    EventLog log;
    size_t i;

    init_rt(&rt, prg, chr);
    memset(&log, 0, sizeof(log));
    ai2_v06_set_bus_observer(&rt, observe, &log);
    rt.ram[0x10] = 0x81u;
    CHECK(ai2_v05_op_06_ASL_Zero(&rt, 0x0200u, 0x0010u, &r) == 0);
    CHECK(rt.ram[0x10] == 0x02u);
    CHECK((rt.p & AI2_V06_FLAG_C) != 0u);
    CHECK(rt.cpu_cycles == 5u && log.count == 5u);
    {
        const Ai2V06BusAccessKind kinds[5] = {
            AI2_V06_BUS_CODE_CYCLE, AI2_V06_BUS_OPERAND_CYCLE, AI2_V06_BUS_READ,
            AI2_V06_BUS_RMW_WRITE_OLD, AI2_V06_BUS_RMW_WRITE_NEW
        };
        const uint16_t addr[5] = {0x0200u,0x0201u,0x0010u,0x0010u,0x0010u};
        const uint8_t val[5] = {0x06u,0x10u,0x81u,0x81u,0x02u};
        for(i=0;i<5;i++) { CHECK(log.events[i].kind==kinds[i]); CHECK(log.events[i].address==addr[i]); CHECK(log.events[i].value==val[i]); }
    }

    init_rt(&rt, prg, chr);
    memset(&log,0,sizeof(log)); ai2_v06_set_bus_observer(&rt,observe,&log);
    rt.x=1u; rt.ram[0x0100u]=0x42u;
    CHECK(ai2_v05_op_BD_LDA_AbsX(&rt,0x0200u,0x00FFu,&r)==0);
    CHECK(rt.a==0x42u && rt.cpu_cycles==5u && log.count==5u);
    CHECK(log.events[3].kind==AI2_V06_BUS_DUMMY_READ && log.events[3].address==0x0000u);
    CHECK(log.events[4].kind==AI2_V06_BUS_READ && log.events[4].address==0x0100u);

    init_rt(&rt, prg, chr);
    memset(&log,0,sizeof(log)); ai2_v06_set_bus_observer(&rt,observe,&log);
    rt.x=1u; rt.ram[0x0011u]=0x24u;
    CHECK(ai2_v05_op_BD_LDA_AbsX(&rt,0x0200u,0x0010u,&r)==0);
    CHECK(rt.a==0x24u && rt.cpu_cycles==4u && log.count==4u);

    init_rt(&rt, prg, chr);
    memset(&log,0,sizeof(log)); ai2_v06_set_bus_observer(&rt,observe,&log);
    rt.p=(uint8_t)(AI2_V06_FLAG_U); /* BPL taken */
    CHECK(ai2_v05_op_10_BPL_Rel(&rt,0x02FEu,0x00FFu,&r)==0);
    CHECK(r.branch_taken==1u && rt.cpu_cycles==4u && log.count==4u);
    CHECK(log.events[2].kind==AI2_V06_BUS_DUMMY_READ && log.events[2].address==0x0300u);
    CHECK(log.events[3].kind==AI2_V06_BUS_DUMMY_READ && log.events[3].address==0x03FFu);

    init_rt(&rt, prg, chr);
    memset(&log,0,sizeof(log)); ai2_v06_set_bus_observer(&rt,observe,&log);
    rt.ram[0x02FFu]=0x34u; rt.ram[0x0200u]=0x12u;
    CHECK(ai2_v05_op_6C_JMP_Ind(&rt,0x0300u,0x02FFu,&r)==0);
    CHECK(r.dynamic_pc==0x1234u && rt.cpu_cycles==5u && log.count==5u);
    CHECK(log.events[3].address==0x02FFu && log.events[4].address==0x0200u);

    init_rt(&rt, prg, chr);
    memset(&log,0,sizeof(log)); ai2_v06_set_bus_observer(&rt,observe,&log);
    CHECK(ai2_v05_op_20_JSR_Abs(&rt,0x0200u,0x1234u,&r)==0);
    CHECK(rt.cpu_cycles==6u && rt.s==0xFBu);
    CHECK(rt.ram[0x01FDu]==0x02u && rt.ram[0x01FCu]==0x02u);
    CHECK(log.events[2].kind==AI2_V06_BUS_DUMMY_READ && log.events[2].address==0x01FDu);
    CHECK(log.events[3].kind==AI2_V06_BUS_STACK_WRITE && log.events[3].address==0x01FDu);
    CHECK(log.events[4].kind==AI2_V06_BUS_STACK_WRITE && log.events[4].address==0x01FCu);

    init_rt(&rt, prg, chr);
    rt.s=0xFBu; rt.ram[0x01FCu]=0x02u; rt.ram[0x01FDu]=0x02u;
    CHECK(ai2_v05_op_60_RTS_Imp(&rt,0x0400u,0u,&r)==0);
    CHECK(r.dynamic_pc==0x0203u && rt.cpu_cycles==6u && rt.s==0xFDu);

    init_rt(&rt, prg, chr);
    rt.s=0xFAu; rt.ram[0x01FBu]=(uint8_t)(AI2_V06_FLAG_C|AI2_V06_FLAG_B); rt.ram[0x01FCu]=0x34u; rt.ram[0x01FDu]=0x12u;
    CHECK(ai2_v05_op_40_RTI_Imp(&rt,0x0400u,0u,&r)==0);
    CHECK(r.dynamic_pc==0x1234u && rt.cpu_cycles==6u && rt.s==0xFDu);
    CHECK((rt.p & AI2_V06_FLAG_C)!=0u && (rt.p & AI2_V06_FLAG_B)==0u && (rt.p & AI2_V06_FLAG_U)!=0u);

    init_rt(&rt, prg, chr);
    rt.s=0u; rt.a=0x5Au;
    CHECK(ai2_v05_op_48_PHA_Imp(&rt,0x0200u,0u,&r)==0);
    CHECK(rt.ram[0x0100u]==0x5Au && rt.s==0xFFu);
}

static void test_alu_exhaustive(const uint8_t *prg, const uint8_t *chr)
{
    Ai2V05Runtime rt;
    Ai2V05SemanticResult r;
    unsigned a, v, c, d;
    init_rt(&rt, prg, chr);

    for(d=0; d<2; d++) for(c=0; c<2; c++) for(a=0; a<256; a++) for(v=0; v<256; v++) {
        unsigned sum = a + v + c;
        unsigned res = sum & 0xFFu;
        unsigned ov = ((~(a ^ v) & (a ^ res) & 0x80u) != 0u);
        rt.a=(uint8_t)a; rt.p=(uint8_t)(AI2_V06_FLAG_U | (c?AI2_V06_FLAG_C:0u) | (d?AI2_V06_FLAG_D:0u));
        CHECK(ai2_v05_op_69_ADC_Imm(&rt,0x0200u,(uint16_t)v,&r)==0);
        CHECK(rt.a==(uint8_t)res);
        CHECK(((rt.p&AI2_V06_FLAG_C)!=0u)==(sum>0xFFu));
        CHECK(((rt.p&AI2_V06_FLAG_V)!=0u)==ov);
        CHECK(((rt.p&AI2_V06_FLAG_Z)!=0u)==(res==0u));
        CHECK(((rt.p&AI2_V06_FLAG_N)!=0u)==((res&0x80u)!=0u));
        CHECK(((rt.p&AI2_V06_FLAG_D)!=0u)==(d!=0u)); /* NES ignores D for arithmetic. */
        CHECK((rt.p&AI2_V06_FLAG_B)==0u && (rt.p&AI2_V06_FLAG_U)!=0u);
    }

    for(d=0; d<2; d++) for(c=0; c<2; c++) for(a=0; a<256; a++) for(v=0; v<256; v++) {
        int diff = (int)a - (int)v - (c ? 0 : 1);
        unsigned res = (unsigned)diff & 0xFFu;
        unsigned ov = (((a ^ res) & (a ^ v) & 0x80u) != 0u);
        rt.a=(uint8_t)a; rt.p=(uint8_t)(AI2_V06_FLAG_U | (c?AI2_V06_FLAG_C:0u) | (d?AI2_V06_FLAG_D:0u));
        CHECK(ai2_v05_op_E9_SBC_Imm(&rt,0x0200u,(uint16_t)v,&r)==0);
        CHECK(rt.a==(uint8_t)res);
        CHECK(((rt.p&AI2_V06_FLAG_C)!=0u)==(diff>=0));
        CHECK(((rt.p&AI2_V06_FLAG_V)!=0u)==ov);
        CHECK(((rt.p&AI2_V06_FLAG_Z)!=0u)==(res==0u));
        CHECK(((rt.p&AI2_V06_FLAG_N)!=0u)==((res&0x80u)!=0u));
        CHECK(((rt.p&AI2_V06_FLAG_D)!=0u)==(d!=0u));
        CHECK((rt.p&AI2_V06_FLAG_B)==0u && (rt.p&AI2_V06_FLAG_U)!=0u);
    }

    for(a=0; a<256; a++) for(v=0; v<256; v++) {
        unsigned res=(a-v)&0xFFu;
        rt.a=(uint8_t)a; rt.p=AI2_V06_FLAG_U;
        CHECK(ai2_v05_op_C9_CMP_Imm(&rt,0x0200u,(uint16_t)v,&r)==0);
        CHECK(((rt.p&AI2_V06_FLAG_C)!=0u)==(a>=v));
        CHECK(((rt.p&AI2_V06_FLAG_Z)!=0u)==(a==v));
        CHECK(((rt.p&AI2_V06_FLAG_N)!=0u)==((res&0x80u)!=0u));
    }

    for(c=0;c<2;c++) for(a=0;a<256;a++) {
        unsigned res;
        rt.a=(uint8_t)a; rt.p=(uint8_t)(AI2_V06_FLAG_U|(c?AI2_V06_FLAG_C:0u));
        CHECK(ai2_v05_op_2A_ROL_Acc(&rt,0x0200u,0u,&r)==0); res=((a<<1)|c)&0xFFu;
        CHECK(rt.a==(uint8_t)res); CHECK(((rt.p&AI2_V06_FLAG_C)!=0u)==((a&0x80u)!=0u));
        rt.a=(uint8_t)a; rt.p=(uint8_t)(AI2_V06_FLAG_U|(c?AI2_V06_FLAG_C:0u));
        CHECK(ai2_v05_op_6A_ROR_Acc(&rt,0x0200u,0u,&r)==0); res=((a>>1)|(c?0x80u:0u));
        CHECK(rt.a==(uint8_t)res); CHECK(((rt.p&AI2_V06_FLAG_C)!=0u)==((a&1u)!=0u));
    }
}

static void test_interrupts(const uint8_t *prg, const uint8_t *chr)
{
    Ai2V05Runtime rt;
    Ai2V05Identity cur, next;
    uint64_t before;
    init_rt(&rt,prg,chr);
    CHECK(ai2_v06_reset(&rt,&cur)==0);
    before=rt.cpu_cycles;
    ai2_v06_request_nmi(&rt);
    CHECK(ai2_v06_step(&rt,cur,&next)==0);
    CHECK(next.bank==15u && next.pc==0xE0CEu);
    CHECK(rt.cpu_cycles-before==10u);
    CHECK(rt.s==0xFAu && rt.nmi_pending==0u);

    cur=next; rt.p=(uint8_t)((rt.p & (uint8_t)~AI2_V06_FLAG_I)|AI2_V06_FLAG_U);
    ai2_v06_set_external_irq(&rt,1);
    before=rt.cpu_cycles;
    CHECK(ai2_v06_step(&rt,cur,&next)==0);
    CHECK(next.bank==15u && next.pc==0xE152u);
    CHECK(rt.cpu_cycles-before==10u);
    CHECK((rt.p&AI2_V06_FLAG_I)!=0u);

    init_rt(&rt,prg,chr); CHECK(ai2_v06_reset(&rt,&cur)==0);
    rt.p=(uint8_t)(rt.p & (uint8_t)~AI2_V06_FLAG_I);
    ai2_v06_set_external_irq(&rt,1); ai2_v06_request_nmi(&rt);
    CHECK(ai2_v06_step(&rt,cur,&next)==0);
    CHECK(next.pc==0xE0CEu); /* NMI wins priority. */
    CHECK(rt.nmi_pending==0u);

    init_rt(&rt,prg,chr); CHECK(ai2_v06_reset(&rt,&cur)==0);
    rt.p=(uint8_t)(rt.p & (uint8_t)~AI2_V06_FLAG_I);
    rt.mapper.irq_pending_known=true; rt.mapper.irq_pending=true;
    CHECK(ai2_v06_step(&rt,cur,&next)==0);
    CHECK(next.pc==0xE152u);
}

int main(int argc, char **argv)
{
    unsigned char *rom;
    size_t size=0;
    const uint8_t *prg,*chr;
    if(argc != 2 && argc != 3) { fprintf(stderr,"usage: %s ROM.nes [--smoke]\n",argv[0]); return 2; }
    if(argc == 3 && strcmp(argv[2], "--smoke") != 0) { fprintf(stderr,"unknown option\n"); return 2; }
    rom=load_file(argv[1],&size);
    if(rom==NULL) { fprintf(stderr,"cannot read ROM\n"); return 2; }
    CHECK(size == 16u + AI2_PRG_ROM_BYTES + AI2_CHR_ROM_BYTES);
    CHECK(size>=16u && memcmp(rom,"NES\x1A",4)==0);
    CHECK(rom[4]==8u && rom[5]==16u);
    CHECK((rom[6]>>4)==4u && (rom[7]&0xF0u)==0u);
    if(failures) { free(rom); return 1; }
    prg=rom+16u; chr=prg+AI2_PRG_ROM_BYTES;

    test_reset_and_bus(prg,chr);
    test_mapper_shared_authority(prg,chr);
    test_bus_order_and_addressing(prg,chr);
    if(argc < 3 || strcmp(argv[2], "--smoke") != 0) {
        test_alu_exhaustive(prg,chr);
    }
    test_interrupts(prg,chr);

    free(rom);
    if(failures) { fprintf(stderr,"V06 runtime: %u failures after %llu checks\n",failures,checks); return 1; }
    printf("V06 runtime construction: PASS (%llu checks)\n",checks);
    return 0;
}
