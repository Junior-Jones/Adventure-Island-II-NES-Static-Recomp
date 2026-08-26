#include "ai2_v08_apu.h"
#include <string.h>

static const uint8_t k_length[32]={10,254,20,2,40,4,80,6,160,8,60,10,14,12,26,14,12,16,24,18,48,20,96,22,192,24,72,26,16,28,32,30};
static const uint8_t k_duty[4][8]={{0,0,0,0,0,0,0,1},{0,0,0,0,0,0,1,1},{0,0,0,0,1,1,1,1},{1,1,1,1,1,1,0,0}};
static const uint8_t k_tri[32]={15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
static const uint16_t k_noise[16]={4,8,16,32,64,96,128,160,202,254,380,508,762,1016,2034,4068};
static const uint16_t k_dmc[16]={428,380,340,320,286,254,226,214,190,160,142,128,106,85,72,54};

static void len_enable(Ai2V08LengthCounter*l,int en){l->enabled=en?1u:0u;if(!en)l->counter=0u;}
static void len_load(Ai2V08LengthCounter*l,uint8_t i){if(l->enabled)l->counter=k_length[i&31u];}
static void len_tick(Ai2V08LengthCounter*l){if(l->counter&&!l->halt)l->counter--;}
static void env_write(Ai2V08Envelope*e,uint8_t v){e->length.halt=(v&0x20u)!=0;e->constant_volume=(v&0x10u)!=0;e->volume_period=v&15u;}
static void env_tick(Ai2V08Envelope*e){if(e->start){e->start=0;e->decay=15;e->divider=e->volume_period;}else if(e->divider)e->divider--;else{e->divider=e->volume_period;if(e->decay)e->decay--;else if(e->length.halt)e->decay=15;}}
static uint8_t env_volume(const Ai2V08Envelope*e){if(!e->length.counter)return 0;return e->constant_volume?e->volume_period:e->decay;}

static uint16_t pulse_target(const Ai2V08Pulse*p){uint16_t d=p->sweep_shift?(uint16_t)(p->period>>p->sweep_shift):0u;if(p->sweep_negate)return(uint16_t)(p->period-d-(p->channel1?1u:0u));return(uint16_t)(p->period+d);}
static int pulse_muted(const Ai2V08Pulse*p){return p->period<8u||(!p->sweep_negate&&pulse_target(p)>0x7ffu);}
static void pulse_out(Ai2V08Pulse*p){p->output=(pulse_muted(p)||!p->envelope.length.counter)?0u:(uint8_t)(k_duty[p->duty&3u][p->duty_pos&7u]*env_volume(&p->envelope));}
static void pulse_timer(Ai2V08Pulse*p){if(!p->timer){p->timer=p->period;p->duty_pos=(uint8_t)((p->duty_pos-1u)&7u);}else p->timer--;pulse_out(p);}
static void pulse_sweep(Ai2V08Pulse*p){int z=!p->sweep_divider;if(z&&p->sweep_enabled&&p->sweep_shift&&!pulse_muted(p)){uint16_t t=pulse_target(p);if(t<=0x7ffu)p->period=t;}if(z||p->sweep_reload){p->sweep_divider=p->sweep_period;p->sweep_reload=0;}else p->sweep_divider--;pulse_out(p);}
static void tri_timer(Ai2V08Triangle*t){if(!t->timer){t->timer=t->period;if(t->length.counter&&t->linear_counter){t->sequence_pos=(uint8_t)((t->sequence_pos+1u)&31u);t->output=k_tri[t->sequence_pos&31u];}}else t->timer--;}
static void tri_linear(Ai2V08Triangle*t){if(t->linear_reload_flag)t->linear_counter=t->linear_reload_value;else if(t->linear_counter)t->linear_counter--;if(!t->control)t->linear_reload_flag=0;}
static void noise_timer(Ai2V08Noise*n){if(!n->timer){uint16_t tap=n->mode?6u:1u;uint16_t fb=(uint16_t)((n->lfsr^(n->lfsr>>tap))&1u);n->lfsr=(uint16_t)((n->lfsr>>1)|(fb<<14));n->timer=(uint16_t)(n->timer_period-1u);}else n->timer--;n->output=((n->lfsr&1u)||!n->envelope.length.counter)?0u:env_volume(&n->envelope);}

static void dmc_init_sample(Ai2V08Dmc*d){d->current_address=d->sample_address;d->bytes_remaining=d->sample_length;}
static void dmc_request(Ai2V08Dmc*d){if(d->enabled&&d->sample_buffer_empty&&d->bytes_remaining)d->dma_pending=1;}
static void dmc_timer(Ai2V08Dmc*d){if(!d->timer){d->timer=(uint16_t)(d->period-1u);if(!d->silence){if(d->shift_register&1u){if(d->output_level<=125u)d->output_level+=2u;}else if(d->output_level>=2u)d->output_level-=2u;}d->shift_register>>=1;if(--d->bits_remaining==0u){d->bits_remaining=8;if(d->sample_buffer_empty)d->silence=1;else{d->silence=0;d->shift_register=d->sample_buffer;d->sample_buffer_empty=1;dmc_request(d);}}}else d->timer--;}

static void quarter(Ai2V08Apu*a){a->quarter_frame_ticks++;env_tick(&a->pulse1.envelope);env_tick(&a->pulse2.envelope);env_tick(&a->noise.envelope);tri_linear(&a->triangle);pulse_out(&a->pulse1);pulse_out(&a->pulse2);}
static void half(Ai2V08Apu*a){a->half_frame_ticks++;quarter(a);len_tick(&a->pulse1.envelope.length);len_tick(&a->pulse2.envelope.length);len_tick(&a->triangle.length);len_tick(&a->noise.envelope.length);pulse_sweep(&a->pulse1);pulse_sweep(&a->pulse2);}
static void update_irq(Ai2V05Runtime*rt,Ai2V08Apu*a){ai2_v06_set_external_irq(rt,a->frame_irq_flag||a->dmc.irq_flag);}
static void frame_tick(Ai2V05Runtime*rt,Ai2V08Apu*a){a->frame_cycle++;if(a->frame_write_delay>0&&--a->frame_write_delay==0){a->frame_mode5=(a->frame_pending_value&0x80u)!=0;a->frame_irq_inhibit=(a->frame_pending_value&0x40u)!=0;if(a->frame_irq_inhibit)a->frame_irq_flag=0;a->frame_cycle=0;if(a->frame_mode5)half(a);}if(!a->frame_mode5){if(a->frame_cycle==7457u||a->frame_cycle==22371u)quarter(a);else if(a->frame_cycle==14913u||a->frame_cycle==29829u)half(a);if(a->frame_cycle>=29828u&&a->frame_cycle<=29830u&&!a->frame_irq_inhibit)a->frame_irq_flag=1;if(a->frame_cycle>=29830u)a->frame_cycle=0;}else{if(a->frame_cycle==7457u||a->frame_cycle==22371u)quarter(a);else if(a->frame_cycle==14913u||a->frame_cycle==37281u)half(a);if(a->frame_cycle>=37282u)a->frame_cycle=0;}update_irq(rt,a);}

int16_t ai2_v08_current_mix(const Ai2V08Apu*a){uint32_t p=(uint32_t)a->pulse1.output+a->pulse2.output;uint32_t xq=(uint32_t)a->triangle.output*(16777216u/8227u)+(uint32_t)a->noise.output*(16777216u/12241u)+(uint32_t)a->dmc.output_level*(16777216u/22638u);uint64_t pn=(uint64_t)9588u*p*32767u,pd=(uint64_t)100u*(8128u+100u*p);uint64_t tn=(uint64_t)15979u*xq*32767u,td=(uint64_t)100u*(16777216u+100u*xq);uint32_t s=(uint32_t)(pn/pd)+(uint32_t)(tn/td);return(int16_t)(s>32767u?32767u:s);}
static void emit_pcm(Ai2V08Apu*a){a->pcm_phase+=AI2_V08_PCM_HZ;while(a->pcm_phase>=AI2_V08_NTSC_CPU_HZ){int16_t s=ai2_v08_current_mix(a);a->pcm_phase-=AI2_V08_NTSC_CPU_HZ;a->pcm_total_samples++;if(a->pcm_count<AI2_V08_PCM_CAPACITY){a->pcm[a->pcm_write]=s;a->pcm_write=(a->pcm_write+1u)%AI2_V08_PCM_CAPACITY;a->pcm_count++;}else a->pcm_dropped++;}}

void ai2_v08_apu_init(Ai2V08Apu*a){if(!a)return;memset(a,0,sizeof(*a));a->pulse1.channel1=1;a->noise.lfsr=1;a->noise.timer_period=k_noise[0];a->dmc.period=k_dmc[0];a->dmc.timer=(uint16_t)(a->dmc.period-1u);a->dmc.sample_address=0xc000u;a->dmc.sample_length=1;a->dmc.sample_buffer_empty=1;a->dmc.bits_remaining=8;a->dmc.silence=1;a->frame_write_delay=3;a->frame_pending_value=0;}
void ai2_v08_machine_init(Ai2V08MachineDevices*d,Ai2V07Ppu*ppu){if(!d)return;memset(d,0,sizeof(*d));d->ppu=ppu;ai2_v08_apu_init(&d->apu);}
void ai2_v08_set_controller_live(Ai2V08Apu*a,unsigned port,uint8_t buttons){if(a&&port<2u)a->controllers[port].live=buttons;}
static void latch(Ai2V08Apu*a){unsigned i;for(i=0;i<2u;i++){a->controllers[i].latched=a->controllers[i].live;a->controllers[i].shift=a->controllers[i].latched;}a->controller_latches++;}
static uint8_t controller_read(Ai2V08Apu*a,unsigned p){Ai2V08Controller*c=&a->controllers[p];uint8_t b;if(a->controller_strobe)b=c->live&1u;else{b=c->shift&1u;c->shift=(uint8_t)((c->shift>>1)|0x80u);}a->controller_reads++;return b;}

static void write_pulse(Ai2V08Pulse *p, uint16_t reg, uint8_t value)
{
    switch(reg & 3u) {
        case 0u:
            p->duty = (uint8_t)(value >> 6);
            env_write(&p->envelope, value);
            break;
        case 1u:
            p->sweep_enabled = (uint8_t)((value >> 7) & 1u);
            p->sweep_period = (uint8_t)(((value >> 4) & 7u) + 1u);
            p->sweep_negate = (uint8_t)((value >> 3) & 1u);
            p->sweep_shift = (uint8_t)(value & 7u);
            p->sweep_reload = 1u;
            break;
        case 2u:
            p->period = (uint16_t)((p->period & 0x0700u) | value);
            break;
        case 3u:
            p->period = (uint16_t)((p->period & 0x00FFu) | ((uint16_t)(value & 7u) << 8));
            len_load(&p->envelope.length, (uint8_t)(value >> 3));
            p->duty_pos = 0u;
            p->envelope.start = 1u;
            break;
        default:
            break;
    }
    pulse_out(p);
}

static void write_triangle(Ai2V08Triangle *t, uint16_t reg, uint8_t value)
{
    switch(reg & 3u) {
        case 0u:
            t->control = (uint8_t)((value >> 7) & 1u);
            t->length.halt = t->control;
            t->linear_reload_value = (uint8_t)(value & 0x7Fu);
            break;
        case 1u:
            /* $4009 is unused. */
            break;
        case 2u:
            t->period = (uint16_t)((t->period & 0x0700u) | value);
            break;
        case 3u:
            t->period = (uint16_t)((t->period & 0x00FFu) | ((uint16_t)(value & 7u) << 8));
            len_load(&t->length, (uint8_t)(value >> 3));
            t->linear_reload_flag = 1u;
            break;
        default:
            break;
    }
}

static void write_noise(Ai2V08Noise *n, uint16_t reg, uint8_t value)
{
    switch(reg & 3u) {
        case 0u:
            env_write(&n->envelope, value);
            break;
        case 1u:
            /* $400D is unused. */
            break;
        case 2u:
            n->mode = (uint8_t)((value >> 7) & 1u);
            n->timer_period = k_noise[value & 15u];
            break;
        case 3u:
            len_load(&n->envelope.length, (uint8_t)(value >> 3));
            n->envelope.start = 1u;
            break;
        default:
            break;
    }
}

static void write_dmc(Ai2V08Dmc *d, uint16_t reg, uint8_t value)
{
    switch(reg & 3u) {
        case 0u:
            d->irq_enabled = (uint8_t)((value >> 7) & 1u);
            d->loop = (uint8_t)((value >> 6) & 1u);
            d->period = k_dmc[value & 15u];
            if(!d->irq_enabled) {
                d->irq_flag = 0u;
            }
            break;
        case 1u:
            d->output_level = (uint8_t)(value & 0x7Fu);
            break;
        case 2u:
            d->sample_address = (uint16_t)(0xC000u | ((uint16_t)value << 6));
            break;
        case 3u:
            d->sample_length = (uint16_t)(((uint16_t)value << 4) | 1u);
            break;
        default:
            break;
    }
}

static uint8_t apu_status(const Ai2V08Apu *a)
{
    uint8_t status = 0u;
    if(a->pulse1.envelope.length.counter) status |= 0x01u;
    if(a->pulse2.envelope.length.counter) status |= 0x02u;
    if(a->triangle.length.counter) status |= 0x04u;
    if(a->noise.envelope.length.counter) status |= 0x08u;
    if(a->dmc.bytes_remaining) status |= 0x10u;
    if(a->frame_irq_flag) status |= 0x40u;
    if(a->dmc.irq_flag) status |= 0x80u;
    return status;
}

int ai2_v08_apu_io_read(Ai2V05Runtime *rt, void *user, uint16_t address, uint8_t *value)
{
    Ai2V08MachineDevices *d = (Ai2V08MachineDevices *)user;
    Ai2V08Apu *a;
    if(rt == NULL || d == NULL || value == NULL) return -1;
    a = &d->apu;
    a->register_reads++;
    switch(address) {
        case 0x4015u:
            *value = (uint8_t)((rt->open_bus & 0x20u) | apu_status(a));
            a->frame_irq_flag = 0u;
            update_irq(rt, a);
            return 0;
        case 0x4016u:
            *value = (uint8_t)((rt->open_bus & 0xE0u) | 0x40u | controller_read(a, 0u));
            return 0;
        case 0x4017u:
            *value = (uint8_t)((rt->open_bus & 0xE0u) | 0x40u | controller_read(a, 1u));
            return 0;
        default:
            /* APU write-only registers and CPU test registers expose open bus. */
            *value = rt->open_bus;
            return 0;
    }
}

int ai2_v08_apu_io_write(Ai2V05Runtime *rt, void *user, uint16_t address, uint8_t value)
{
    Ai2V08MachineDevices *d = (Ai2V08MachineDevices *)user;
    Ai2V08Apu *a;
    if(rt == NULL || d == NULL) return -1;
    a = &d->apu;
    a->register_writes++;
    if(address >= 0x4000u && address <= 0x4003u) {
        write_pulse(&a->pulse1, address, value);
    } else if(address >= 0x4004u && address <= 0x4007u) {
        write_pulse(&a->pulse2, address, value);
    } else if(address >= 0x4008u && address <= 0x400Bu) {
        write_triangle(&a->triangle, address, value);
    } else if(address >= 0x400Cu && address <= 0x400Fu) {
        write_noise(&a->noise, address, value);
    } else if(address >= 0x4010u && address <= 0x4013u) {
        write_dmc(&a->dmc, address, value);
    } else if(address == 0x4015u) {
        len_enable(&a->pulse1.envelope.length, (value & 0x01u) != 0u);
        len_enable(&a->pulse2.envelope.length, (value & 0x02u) != 0u);
        len_enable(&a->triangle.length, (value & 0x04u) != 0u);
        len_enable(&a->noise.envelope.length, (value & 0x08u) != 0u);
        a->dmc.enabled = (uint8_t)((value >> 4) & 1u);
        a->dmc.irq_flag = 0u;
        if(!a->dmc.enabled) {
            a->dmc.bytes_remaining = 0u;
            a->dmc.dma_pending = 0u;
        } else if(a->dmc.bytes_remaining == 0u) {
            dmc_init_sample(&a->dmc);
            dmc_request(&a->dmc);
        }
        update_irq(rt, a);
    } else if(address == 0x4016u) {
        uint8_t old = a->controller_strobe;
        a->controller_strobe = (uint8_t)(value & 1u);
        if(a->controller_strobe || old) latch(a);
    } else if(address == 0x4017u) {
        a->frame_pending_value = value;
        a->frame_write_delay = (int8_t)((rt->cpu_cycles & 1u) ? 4 : 3);
        a->frame_irq_inhibit = (uint8_t)((value >> 6) & 1u);
        if(a->frame_irq_inhibit) a->frame_irq_flag = 0u;
        update_irq(rt, a);
    }
    return 0;
}

int ai2_v08_oam_dma_start(Ai2V05Runtime *rt, void *user, uint16_t address, uint8_t value)
{
    Ai2V08MachineDevices *d = (Ai2V08MachineDevices *)user;
    (void)address;
    if(rt == NULL || d == NULL || d->ppu == NULL) return -1;
    d->apu.oam_dma_pending = 1u;
    d->apu.oam_dma_page = value;
    return 0;
}

static void dmc_supply(Ai2V05Runtime *rt, Ai2V08Apu *a, uint8_t value)
{
    Ai2V08Dmc *d = &a->dmc;
    d->sample_buffer = value;
    d->sample_buffer_empty = 0u;
    d->dma_pending = 0u;
    d->current_address = d->current_address == 0xFFFFu ? 0x8000u : (uint16_t)(d->current_address + 1u);
    if(d->bytes_remaining) d->bytes_remaining--;
    if(d->bytes_remaining == 0u) {
        if(d->loop) {
            dmc_init_sample(d);
        } else if(d->irq_enabled) {
            d->irq_flag = 1u;
        }
    }
    dmc_request(d);
    update_irq(rt, a);
}

static void service_dmc(Ai2V05Runtime *rt, Ai2V08Apu *a, int collision)
{
    uint8_t value = 0u;
    if(!a->dmc.dma_pending || rt->trap.code != AI2_V06_TRAP_NONE) return;
    if(collision) a->dma_collision_count++;
    /* NES DMC steals four CPU cycles for a normal sample fetch.  Keeping all
     * four cycles on the common runtime scheduler preserves PPU/APU/MMC3 time. */
    ai2_v06_internal_cycle(rt);
    ai2_v06_internal_cycle(rt);
    ai2_v06_internal_cycle(rt);
    if(ai2_v06_bus_read(rt, a->dmc.current_address, AI2_V06_BUS_READ, &value) != 0) return;
    a->dmc_dma_count++;
    a->dmc_dma_cycles += 4u;
    dmc_supply(rt, a, value);
}

void ai2_v08_dma_drain(Ai2V05Runtime *rt, void *user)
{
    Ai2V08MachineDevices *d = (Ai2V08MachineDevices *)user;
    Ai2V08Apu *a;
    uint64_t before;
    unsigned i;
    if(rt == NULL || d == NULL) return;
    a = &d->apu;
    if(a->oam_dma_pending) {
        uint16_t base = (uint16_t)((uint16_t)a->oam_dma_page << 8);
        before = rt->cpu_cycles;
        a->oam_dma_pending = 0u;
        a->oam_dma_count++;
        ai2_v06_internal_cycle(rt);
        if((before & 1u) != 0u) ai2_v06_internal_cycle(rt);
        for(i = 0; i < 256u && rt->trap.code == AI2_V06_TRAP_NONE; i++) {
            uint8_t value = 0u;
            if(a->dmc.dma_pending) service_dmc(rt, a, 1);
            if(ai2_v06_bus_read(rt, (uint16_t)(base + i), AI2_V06_BUS_READ, &value) != 0) break;
            if(a->dmc.dma_pending) service_dmc(rt, a, 1);
            if(ai2_v06_bus_write(rt, 0x2004u, value, AI2_V06_BUS_WRITE) != 0) break;
        }
        a->oam_dma_cycles += rt->cpu_cycles - before;
    } else {
        service_dmc(rt, a, 0);
    }
}

void ai2_v08_dma_tick(Ai2V05Runtime *rt, void *user)
{
    (void)rt;
    (void)user;
    /* Arbitration is drained after each completed CPU cycle by V06. */
}

void ai2_v08_apu_tick(Ai2V05Runtime *rt, void *user)
{
    Ai2V08MachineDevices *d = (Ai2V08MachineDevices *)user;
    Ai2V08Apu *a;
    if(rt == NULL || d == NULL) return;
    a = &d->apu;
    a->cycle++;
    frame_tick(rt, a);
    tri_timer(&a->triangle);
    if((a->cycle & 1u) == 0u) {
        pulse_timer(&a->pulse1);
        pulse_timer(&a->pulse2);
        noise_timer(&a->noise);
    }
    dmc_timer(&a->dmc);
    dmc_request(&a->dmc);
    emit_pcm(a);
    update_irq(rt, a);
}

static int ppu_read_wrap(Ai2V05Runtime *rt, void *user, uint16_t address, uint8_t *value)
{
    Ai2V08MachineDevices *d = (Ai2V08MachineDevices *)user;
    return d != NULL && d->ppu != NULL ? ai2_v07_ppu_cpu_read(rt, d->ppu, address, value) : -1;
}

static int ppu_write_wrap(Ai2V05Runtime *rt, void *user, uint16_t address, uint8_t value)
{
    Ai2V08MachineDevices *d = (Ai2V08MachineDevices *)user;
    return d != NULL && d->ppu != NULL ? ai2_v07_ppu_cpu_write(rt, d->ppu, address, value) : -1;
}

static void ppu_tick_wrap(Ai2V05Runtime *rt, void *user)
{
    Ai2V08MachineDevices *d = (Ai2V08MachineDevices *)user;
    if(d != NULL && d->ppu != NULL) ai2_v07_ppu_tick(rt, d->ppu);
}

void ai2_v08_device_reset(Ai2V05Runtime *rt, void *user, int soft_reset)
{
    Ai2V08MachineDevices *d = (Ai2V08MachineDevices *)user;
    uint8_t live0, live1;
    (void)soft_reset;
    if(rt == NULL || d == NULL) return;
    live0 = d->apu.controllers[0].live;
    live1 = d->apu.controllers[1].live;
    ai2_v08_apu_init(&d->apu);
    d->apu.controllers[0].live = live0;
    d->apu.controllers[1].live = live1;
    if(d->ppu != NULL) ai2_v07_ppu_init(d->ppu, 0u, 0u);
    ai2_v06_set_external_irq(rt, 0);
    ai2_v06_clear_nmi(rt);
}

void ai2_v08_bind(Ai2V05Runtime *rt, Ai2V08MachineDevices *devices)
{
    Ai2V06DeviceHooks hooks;
    if(rt == NULL || devices == NULL || devices->ppu == NULL) return;
    memset(&hooks, 0, sizeof(hooks));
    hooks.user = devices;
    hooks.ppu_read = ppu_read_wrap;
    hooks.ppu_write = ppu_write_wrap;
    hooks.apu_io_read = ai2_v08_apu_io_read;
    hooks.apu_io_write = ai2_v08_apu_io_write;
    hooks.oam_dma_start = ai2_v08_oam_dma_start;
    hooks.ppu_tick = ppu_tick_wrap;
    hooks.apu_tick = ai2_v08_apu_tick;
    hooks.dma_tick = ai2_v08_dma_tick;
    hooks.reset = ai2_v08_device_reset;
    hooks.dma_drain = ai2_v08_dma_drain;
    ai2_v06_set_devices(rt, &hooks);
}

size_t ai2_v08_copy_pcm(Ai2V08Apu *a, int16_t *dst, size_t capacity)
{
    size_t n = 0u;
    if(a == NULL || dst == NULL) return 0u;
    while(n < capacity && a->pcm_count) {
        dst[n++] = a->pcm[a->pcm_read];
        a->pcm_read = (a->pcm_read + 1u) % AI2_V08_PCM_CAPACITY;
        a->pcm_count--;
    }
    return n;
}
