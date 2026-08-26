#ifndef AI2_V08_APU_H
#define AI2_V08_APU_H

#include <stddef.h>
#include <stdint.h>
#include "src/runtime/ai2_v06_runtime.h"
#include "src/ppu/ai2_v07_ppu.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { AI2_V08_NTSC_CPU_HZ=1789773, AI2_V08_PCM_HZ=44100, AI2_V08_PCM_CAPACITY=8192 };
enum Ai2V08Buttons { AI2_V08_BTN_A=0x01, AI2_V08_BTN_B=0x02, AI2_V08_BTN_SELECT=0x04, AI2_V08_BTN_START=0x08,
                     AI2_V08_BTN_UP=0x10, AI2_V08_BTN_DOWN=0x20, AI2_V08_BTN_LEFT=0x40, AI2_V08_BTN_RIGHT=0x80 };

typedef struct { uint8_t enabled,halt,counter; } Ai2V08LengthCounter;
typedef struct { Ai2V08LengthCounter length; uint8_t constant_volume,volume_period,start,divider,decay; } Ai2V08Envelope;
typedef struct {
    Ai2V08Envelope envelope; uint16_t period,timer; uint8_t duty,duty_pos,sweep_enabled,sweep_period,sweep_negate,
    sweep_shift,sweep_reload,sweep_divider,channel1,output;
} Ai2V08Pulse;
typedef struct {
    Ai2V08LengthCounter length; uint16_t period,timer; uint8_t sequence_pos,control,linear_reload_value,linear_counter,
    linear_reload_flag,output;
} Ai2V08Triangle;
typedef struct { Ai2V08Envelope envelope; uint16_t timer_period,timer,lfsr; uint8_t mode,output; } Ai2V08Noise;
typedef struct {
    uint16_t period,timer,sample_address,sample_length,current_address,bytes_remaining;
    uint8_t output_level,irq_enabled,loop,irq_flag,sample_buffer,sample_buffer_empty,shift_register,bits_remaining,
    silence,enabled,dma_pending;
} Ai2V08Dmc;
typedef struct { uint8_t live,latched,shift; } Ai2V08Controller;

typedef struct Ai2V08Apu {
    Ai2V08Pulse pulse1,pulse2; Ai2V08Triangle triangle; Ai2V08Noise noise; Ai2V08Dmc dmc; Ai2V08Controller controllers[2];
    uint64_t cycle; uint32_t frame_cycle; uint8_t frame_mode5,frame_irq_inhibit,frame_irq_flag; int8_t frame_write_delay;
    uint8_t frame_pending_value,controller_strobe,oam_dma_pending,oam_dma_page;
    uint64_t oam_dma_count,oam_dma_cycles,dmc_dma_count,dmc_dma_cycles,dma_collision_count;
    uint64_t quarter_frame_ticks,half_frame_ticks,register_reads,register_writes,controller_reads,controller_latches;
    uint32_t pcm_phase; int16_t pcm[AI2_V08_PCM_CAPACITY]; size_t pcm_read,pcm_write,pcm_count;
    uint64_t pcm_total_samples,pcm_dropped;
} Ai2V08Apu;

typedef struct Ai2V08MachineDevices { Ai2V07Ppu *ppu; Ai2V08Apu apu; } Ai2V08MachineDevices;

void ai2_v08_apu_init(Ai2V08Apu *apu);
void ai2_v08_machine_init(Ai2V08MachineDevices *devices, Ai2V07Ppu *ppu);
void ai2_v08_bind(Ai2V05Runtime *rt, Ai2V08MachineDevices *devices);
void ai2_v08_set_controller_live(Ai2V08Apu *apu, unsigned port, uint8_t buttons);
size_t ai2_v08_copy_pcm(Ai2V08Apu *apu, int16_t *dst, size_t capacity);
int16_t ai2_v08_current_mix(const Ai2V08Apu *apu);

int ai2_v08_apu_io_read(Ai2V05Runtime *rt, void *user, uint16_t address, uint8_t *value);
int ai2_v08_apu_io_write(Ai2V05Runtime *rt, void *user, uint16_t address, uint8_t value);
int ai2_v08_oam_dma_start(Ai2V05Runtime *rt, void *user, uint16_t address, uint8_t value);
void ai2_v08_apu_tick(Ai2V05Runtime *rt, void *user);
void ai2_v08_dma_tick(Ai2V05Runtime *rt, void *user);
void ai2_v08_dma_drain(Ai2V05Runtime *rt, void *user);
void ai2_v08_device_reset(Ai2V05Runtime *rt, void *user, int soft_reset);

#ifdef __cplusplus
}
#endif
#endif
