#include "include/ai2_static_core.h"
#include "src/api/ai2_sha256.h"
#include "src/runtime/ai2_v06_runtime.h"
#include "src/ppu/ai2_v07_ppu.h"
#include "src/apu/ai2_v08_apu.h"
#include "generated/v09/ai2_v09_identity.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#include <process.h>
#include <windows.h>
#define AI2_FILENO _fileno
#define AI2_FSYNC _commit
#define AI2_GETPID _getpid
#else
#include <unistd.h>
#define AI2_FILENO fileno
#define AI2_FSYNC fsync
#define AI2_GETPID getpid
#endif

#define AI2_ROM_FILE_BYTES (16u + AI2_PRG_ROM_BYTES + AI2_CHR_ROM_BYTES)
#define AI2_SNAPSHOT_VERSION 1u
#define AI2_STATE_VERSION 1u
#define AI2_SNAPSHOT_MAX_BYTES (2u * 1024u * 1024u)

static const uint8_t k_snapshot_magic[8] = {'A','I','2','S','N','A','P','1'};

struct Ai2StaticCore {
    uint8_t prg_rom[AI2_PRG_ROM_BYTES];
    uint8_t chr_rom[AI2_CHR_ROM_BYTES];
    uint8_t rom_digest[32];
    uint8_t loaded;

    Ai2V05Runtime runtime;
    Ai2V07Ppu ppu;
    Ai2V08MachineDevices devices;
    Ai2V05Identity current;
    uint8_t pending_buttons[2];
    uint64_t executed_instructions;

    Ai2CoreStatus status;
    uint8_t busy;
    uint8_t in_hook;
    uint8_t stop_requested;
    uint8_t stop_after_instruction;
    uint8_t stopped;
    uint32_t hook_mask;
    Ai2RuntimeHook hook;
    void *hook_user;
    uint64_t event_sequence;
};

typedef struct ByteWriter {
    uint8_t *data;
    size_t size;
    size_t capacity;
    int ok;
} ByteWriter;

typedef struct ByteReader {
    const uint8_t *data;
    size_t size;
    size_t pos;
    int ok;
} ByteReader;

static void set_error(char *error, size_t capacity, const char *text)
{
    if(error == NULL || capacity == 0u) return;
    if(text == NULL) text = "";
    snprintf(error, capacity, "%s", text);
}

static int digest_equal(const uint8_t a[32], const uint8_t b[32])
{
    unsigned i;
    uint8_t diff = 0u;
    for(i = 0; i < 32u; i++) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0u;
}

static Ai2CoreTrapInfo trap_info_from_runtime(const Ai2V05Runtime *rt)
{
    Ai2CoreTrapInfo out;
    const Ai2V06Trap *t = ai2_v06_get_trap(rt);
    memset(&out, 0, sizeof(out));
    if(t != NULL) {
        out.runtime_trap_code = (uint32_t)t->code;
        out.source_pc = t->source_pc;
        out.address = t->address;
        out.value = t->value;
        out.is_write = t->is_write;
        out.physical_prg_bank = t->physical_bank;
        out.cpu_cycle = t->cpu_cycle;
        out.ppu_cycle = t->ppu_cycle;
        out.apu_cycle = t->apu_cycle;
    }
    return out;
}

static int core_safe_boundary(const Ai2StaticCore *core)
{
    return core != NULL && core->loaded && !core->busy && !core->in_hook &&
           core->runtime.dma_draining == 0u;
}

static void core_bind_machine(Ai2StaticCore *core)
{
    core->runtime.prg_rom = core->prg_rom;
    core->runtime.prg_rom_size = AI2_PRG_ROM_BYTES;
    core->runtime.chr_rom = core->chr_rom;
    core->runtime.chr_rom_size = AI2_CHR_ROM_BYTES;
    core->devices.ppu = &core->ppu;
    ai2_v08_bind(&core->runtime, &core->devices);
}

static void core_bus_observer(void *user, const Ai2V06BusEvent *bus);

static void core_bind_observer(Ai2StaticCore *core)
{
    ai2_v06_set_bus_observer(&core->runtime, core_bus_observer, core);
}

static int core_reset_machine(Ai2StaticCore *core)
{
    ai2_v06_init(&core->runtime, core->prg_rom, AI2_PRG_ROM_BYTES,
                 core->chr_rom, AI2_CHR_ROM_BYTES, 0u);
    ai2_v07_ppu_init(&core->ppu, 0u, 0xFFu);
    ai2_v08_machine_init(&core->devices, &core->ppu);
    ai2_v08_set_controller_live(&core->devices.apu, 0u, core->pending_buttons[0]);
    ai2_v08_set_controller_live(&core->devices.apu, 1u, core->pending_buttons[1]);
    core_bind_machine(core);
    core_bind_observer(core);
    if(ai2_v06_reset(&core->runtime, &core->current) != 0) return 0;
    core->executed_instructions = 0u;
    core->stop_requested = 0u;
    core->stop_after_instruction = 0u;
    core->stopped = 0u;
    core->event_sequence = 0u;
    return !ai2_v06_has_trap(&core->runtime);
}

static Ai2RuntimeHookAction core_emit_event(Ai2StaticCore *core,
                                            Ai2RuntimeEventType type,
                                            uint16_t bus_address,
                                            uint8_t bus_value,
                                            uint8_t bus_is_write)
{
    Ai2RuntimeEvent event;
    Ai2RuntimeHookAction action;
    if(core == NULL || core->hook == NULL || (core->hook_mask & (uint32_t)type) == 0u) {
        return AI2_RUNTIME_HOOK_CONTINUE;
    }
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.sequence = ++core->event_sequence;
    event.instruction_index = core->executed_instructions;
    event.cpu_cycle = core->runtime.cpu_cycles;
    event.ppu_cycle = core->runtime.ppu_cycles;
    event.frame = core->ppu.frame_generation;
    event.physical_prg_bank = core->current.bank;
    event.program_counter = core->current.pc;
    event.bus_address = bus_address;
    event.bus_value = bus_value;
    event.bus_is_write = bus_is_write;
    event.trap = trap_info_from_runtime(&core->runtime);
    core->in_hook = 1u;
    action = core->hook(&event, core->hook_user);
    core->in_hook = 0u;
    return action == AI2_RUNTIME_HOOK_STOP ? AI2_RUNTIME_HOOK_STOP : AI2_RUNTIME_HOOK_CONTINUE;
}

static void core_bus_observer(void *user, const Ai2V06BusEvent *bus)
{
    Ai2StaticCore *core = (Ai2StaticCore *)user;
    Ai2RuntimeEventType type;
    if(core == NULL || bus == NULL) return;
    type = bus->is_write ? AI2_RUNTIME_EVENT_BUS_WRITE : AI2_RUNTIME_EVENT_BUS_READ;
    if(core_emit_event(core, type, bus->address, bus->value, bus->is_write) == AI2_RUNTIME_HOOK_STOP) {
        core->stop_after_instruction = 1u;
    }
}

static int validate_rom(const void *rom_bytes, size_t rom_size, uint8_t digest[32])
{
    const uint8_t *rom = (const uint8_t *)rom_bytes;
    if(rom == NULL || rom_size != AI2_ROM_FILE_BYTES) return 0;
    if(memcmp(rom, "NES\x1A", 4u) != 0) return 0;
    if(rom[4] != 8u || rom[5] != 16u || rom[6] != 0x40u || rom[7] != 0u) return 0;
    if(memcmp(rom, "\x4e\x45\x53\x1a\x08\x10\x40\x00\x00\x00\x00\x00\x00\x00\x00\x00", 16u) != 0) return 0;
    ai2_sha256(rom, rom_size, digest);
    return digest_equal(digest, AI2_V09_ROM_DIGEST);
}

Ai2StaticCore *ai2_static_core_create(void)
{
    Ai2StaticCore *core = (Ai2StaticCore *)calloc(1u, sizeof(Ai2StaticCore));
    if(core != NULL) core->status = AI2_CORE_STATUS_OK;
    return core;
}

void ai2_static_core_destroy(Ai2StaticCore *core)
{
    if(core == NULL) return;
    memset(core, 0, sizeof(*core));
    free(core);
}

int ai2_static_core_reset(Ai2StaticCore *core, const void *rom_bytes, size_t rom_size)
{
    uint8_t digest[32];
    const uint8_t *rom = (const uint8_t *)rom_bytes;
    Ai2RuntimeHook hook;
    void *hook_user;
    uint32_t hook_mask;
    if(core == NULL || rom_bytes == NULL) return 0;
    if(core->busy || core->in_hook) {
        core->status = AI2_CORE_STATUS_REENTRANT_CALL;
        return 0;
    }
    if(!validate_rom(rom_bytes, rom_size, digest)) {
        core->status = AI2_CORE_STATUS_BAD_ROM;
        return 0;
    }
    hook = core->hook;
    hook_user = core->hook_user;
    hook_mask = core->hook_mask;
    memset(core, 0, sizeof(*core));
    core->hook = hook;
    core->hook_user = hook_user;
    core->hook_mask = hook_mask;
    memcpy(core->prg_rom, rom + 16u, AI2_PRG_ROM_BYTES);
    memcpy(core->chr_rom, rom + 16u + AI2_PRG_ROM_BYTES, AI2_CHR_ROM_BYTES);
    memcpy(core->rom_digest, digest, sizeof(digest));
    core->loaded = 1u;
    core->status = AI2_CORE_STATUS_OK;
    if(!core_reset_machine(core)) {
        core->status = AI2_CORE_STATUS_INVALID_STATE;
        return 0;
    }
    return 1;
}

void ai2_static_core_set_controller(Ai2StaticCore *core, unsigned port, uint8_t buttons)
{
    if(core == NULL || port >= 2u) return;
    if(core->busy || core->in_hook) {
        core->status = AI2_CORE_STATUS_REENTRANT_CALL;
        return;
    }
    if(!core->loaded) {
        core->status = AI2_CORE_STATUS_NOT_LOADED;
        return;
    }
    core->pending_buttons[port] = buttons;
    ai2_v08_set_controller_live(&core->devices.apu, port, buttons);
    core->status = AI2_CORE_STATUS_OK;
}

static void fill_frame_result(Ai2StaticCore *core, Ai2FrameResult *result,
                              uint64_t start_frame, uint64_t start_instructions,
                              int completed)
{
    if(result == NULL) return;
    memset(result, 0, sizeof(*result));
    result->completed = completed ? 1u : 0u;
    result->stopped = core->stopped;
    result->player1_buttons = core->pending_buttons[0];
    result->player2_buttons = core->pending_buttons[1];
    result->physical_prg_bank = core->current.bank;
    result->program_counter = core->current.pc;
    result->start_frame = start_frame;
    result->end_frame = core->ppu.frame_generation;
    result->executed_instructions = core->executed_instructions - start_instructions;
    result->trap = trap_info_from_runtime(&core->runtime);
}

int ai2_static_core_advance_frame(Ai2StaticCore *core,
                                  uint8_t player1_buttons,
                                  uint8_t player2_buttons,
                                  uint64_t instruction_limit,
                                  Ai2FrameResult *result)
{
    uint64_t start_frame, start_instructions, limit, ran = 0u;
    int completed = 0;
    if(core == NULL) return 0;
    if(core->busy || core->in_hook) {
        core->status = AI2_CORE_STATUS_REENTRANT_CALL;
        return 0;
    }
    if(!core->loaded) {
        core->status = AI2_CORE_STATUS_NOT_LOADED;
        return 0;
    }
    if(ai2_v06_has_trap(&core->runtime)) {
        core->status = AI2_CORE_STATUS_INVALID_STATE;
        fill_frame_result(core, result, core->ppu.frame_generation, core->executed_instructions, 0);
        return 0;
    }
    core->pending_buttons[0] = player1_buttons;
    core->pending_buttons[1] = player2_buttons;
    ai2_v08_set_controller_live(&core->devices.apu, 0u, player1_buttons);
    ai2_v08_set_controller_live(&core->devices.apu, 1u, player2_buttons);
    start_frame = core->ppu.frame_generation;
    start_instructions = core->executed_instructions;
    limit = instruction_limit == 0u ? AI2_CORE_DEFAULT_FRAME_INSTRUCTION_LIMIT : instruction_limit;
    core->status = AI2_CORE_STATUS_OK;
    core->busy = 1u;

    if(core->stopped) {
        core->busy = 0u;
        fill_frame_result(core, result, start_frame, start_instructions, 0);
        return 1;
    }

    while(ran < limit) {
        Ai2V05Identity next;
        uint64_t frame_before;
        if(core->stop_requested) {
            core->stopped = 1u;
            core->stop_requested = 0u;
            break;
        }
        if(core_emit_event(core, AI2_RUNTIME_EVENT_INSTRUCTION_BEFORE, 0u, 0u, 0u) == AI2_RUNTIME_HOOK_STOP) {
            core->stopped = 1u;
            break;
        }
        frame_before = core->ppu.frame_generation;
        if(ai2_v06_step(&core->runtime, core->current, &next) != 0) {
            core_emit_event(core, AI2_RUNTIME_EVENT_FRONTIER, 0u, 0u, 0u);
            core->stopped = 1u;
            break;
        }
        core->current = next;
        core->executed_instructions++;
        ran++;
        if(core_emit_event(core, AI2_RUNTIME_EVENT_INSTRUCTION_AFTER, 0u, 0u, 0u) == AI2_RUNTIME_HOOK_STOP) {
            core->stop_after_instruction = 1u;
        }
        if(core->ppu.frame_generation != frame_before) {
            completed = 1;
            if(core_emit_event(core, AI2_RUNTIME_EVENT_FRAME_COMPLETE, 0u, 0u, 0u) == AI2_RUNTIME_HOOK_STOP) {
                core->stop_after_instruction = 1u;
            }
        }
        if(core->stop_after_instruction || core->stop_requested) {
            core->stop_after_instruction = 0u;
            core->stop_requested = 0u;
            core->stopped = 1u;
            break;
        }
        if(completed) break;
    }
    core->busy = 0u;

    if(ai2_v06_has_trap(&core->runtime)) {
        core->status = AI2_CORE_STATUS_INVALID_STATE;
        fill_frame_result(core, result, start_frame, start_instructions, 0);
        return 0;
    }
    if(!completed && !core->stopped && ran >= limit) {
        core->status = AI2_CORE_STATUS_STEP_LIMIT;
        core->stopped = 1u;
        fill_frame_result(core, result, start_frame, start_instructions, 0);
        return 0;
    }
    fill_frame_result(core, result, start_frame, start_instructions, completed);
    return 1;
}

int ai2_static_core_frame_copy_indexed(const Ai2StaticCore *core,
                                       uint16_t *output,
                                       size_t pixel_capacity)
{
    if(core == NULL || core->in_hook || !core->loaded || output == NULL || pixel_capacity < AI2_CORE_FRAME_PIXELS) return 0;
    return ai2_v07_ppu_copy_indexed(&core->ppu, output, pixel_capacity, NULL) == 0;
}

int ai2_static_core_frame_copy_bgra(const Ai2StaticCore *core,
                                    uint32_t *output,
                                    size_t pixel_capacity)
{
    if(core == NULL || core->in_hook || !core->loaded || output == NULL || pixel_capacity < AI2_CORE_FRAME_PIXELS) return 0;
    return ai2_v07_ppu_copy_bgra(&core->ppu, output, pixel_capacity, NULL) == 0;
}

size_t ai2_static_core_audio_available(const Ai2StaticCore *core)
{
    return core == NULL || core->in_hook || !core->loaded ? 0u : core->devices.apu.pcm_count;
}

size_t ai2_static_core_audio_read(Ai2StaticCore *core,
                                  int16_t *output,
                                  size_t sample_capacity)
{
    if(core == NULL || !core->loaded || output == NULL || sample_capacity == 0u) return 0u;
    if(core->busy || core->in_hook) {
        core->status = AI2_CORE_STATUS_REENTRANT_CALL;
        return 0u;
    }
    core->status = AI2_CORE_STATUS_OK;
    return ai2_v08_copy_pcm(&core->devices.apu, output, sample_capacity);
}

void ai2_static_core_audio_clear(Ai2StaticCore *core)
{
    if(core == NULL || !core->loaded) return;
    if(core->busy || core->in_hook) {
        core->status = AI2_CORE_STATUS_REENTRANT_CALL;
        return;
    }
    core->devices.apu.pcm_read = 0u;
    core->devices.apu.pcm_write = 0u;
    core->devices.apu.pcm_count = 0u;
    core->status = AI2_CORE_STATUS_OK;
}

Ai2CoreStatus ai2_static_core_status(const Ai2StaticCore *core)
{
    if(core == NULL) return AI2_CORE_STATUS_INVALID_ARGUMENT;
    return core->in_hook ? AI2_CORE_STATUS_REENTRANT_CALL : core->status;
}

const char *ai2_static_core_status_name(Ai2CoreStatus status)
{
    switch(status) {
    case AI2_CORE_STATUS_OK: return "ok";
    case AI2_CORE_STATUS_INVALID_ARGUMENT: return "invalid_argument";
    case AI2_CORE_STATUS_NO_MEMORY: return "no_memory";
    case AI2_CORE_STATUS_BAD_ROM: return "bad_rom";
    case AI2_CORE_STATUS_NOT_LOADED: return "not_loaded";
    case AI2_CORE_STATUS_REENTRANT_CALL: return "reentrant_call";
    case AI2_CORE_STATUS_NOT_SAFE_BOUNDARY: return "not_safe_boundary";
    case AI2_CORE_STATUS_IO_ERROR: return "io_error";
    case AI2_CORE_STATUS_BAD_SNAPSHOT: return "bad_snapshot";
    case AI2_CORE_STATUS_SNAPSHOT_VERSION: return "snapshot_version";
    case AI2_CORE_STATUS_ROM_MISMATCH: return "rom_mismatch";
    case AI2_CORE_STATUS_CORE_MISMATCH: return "core_mismatch";
    case AI2_CORE_STATUS_SNAPSHOT_HASH_MISMATCH: return "snapshot_hash_mismatch";
    case AI2_CORE_STATUS_INVALID_STATE: return "invalid_state";
    case AI2_CORE_STATUS_STEP_LIMIT: return "step_limit";
    default: return "unknown";
    }
}

int ai2_static_core_trap(const Ai2StaticCore *core, Ai2CoreTrapInfo *trap)
{
    if(core == NULL || core->in_hook || trap == NULL) return 0;
    *trap = trap_info_from_runtime(&core->runtime);
    return 1;
}

int ai2_static_core_observe(const Ai2StaticCore *core, Ai2CoreObservation *observation)
{
    if(core == NULL || core->in_hook || observation == NULL || !core->loaded) return 0;
    memset(observation, 0, sizeof(*observation));
    observation->status = core->status;
    observation->trap = trap_info_from_runtime(&core->runtime);
    observation->physical_prg_bank = core->current.bank;
    observation->program_counter = core->current.pc;
    observation->player1_pending = core->pending_buttons[0];
    observation->player2_pending = core->pending_buttons[1];
    observation->stopped = core->stopped;
    observation->executed_instructions = core->executed_instructions;
    observation->cpu_cycles = core->runtime.cpu_cycles;
    observation->ppu_cycles = core->runtime.ppu_cycles;
    observation->apu_cycles = core->runtime.apu_cycles;
    observation->frames = core->ppu.frame_generation;
    observation->controller_reads = core->devices.apu.controller_reads;
    observation->controller_latches = core->devices.apu.controller_latches;
    observation->pcm_total_samples = core->devices.apu.pcm_total_samples;
    observation->pcm_available = core->devices.apu.pcm_count;
    return 1;
}

void ai2_static_core_set_runtime_hook(Ai2StaticCore *core,
                                      uint32_t event_mask,
                                      Ai2RuntimeHook hook,
                                      void *user_data)
{
    if(core == NULL) return;
    if(core->busy || core->in_hook) {
        core->status = AI2_CORE_STATUS_REENTRANT_CALL;
        return;
    }
    core->hook_mask = event_mask & (uint32_t)AI2_RUNTIME_EVENT_ALL;
    core->hook = hook;
    core->hook_user = user_data;
    core->status = AI2_CORE_STATUS_OK;
}

void ai2_static_core_clear_runtime_hook(Ai2StaticCore *core)
{
    if(core == NULL) return;
    if(core->busy || core->in_hook) {
        core->status = AI2_CORE_STATUS_REENTRANT_CALL;
        return;
    }
    core->hook_mask = 0u;
    core->hook = NULL;
    core->hook_user = NULL;
    core->status = AI2_CORE_STATUS_OK;
}

void ai2_static_core_request_stop(Ai2StaticCore *core)
{
    if(core == NULL) return;
    if(core->in_hook || core->busy) {
        core->stop_requested = 1u;
        return;
    }
    core->stopped = 1u;
}

int ai2_static_core_is_stopped(const Ai2StaticCore *core)
{
    return core != NULL && !core->in_hook && core->stopped != 0u;
}

void ai2_static_core_resume(Ai2StaticCore *core)
{
    if(core == NULL) return;
    if(core->busy || core->in_hook) {
        core->status = AI2_CORE_STATUS_REENTRANT_CALL;
        return;
    }
    core->stop_requested = 0u;
    core->stop_after_instruction = 0u;
    core->stopped = 0u;
    core->status = AI2_CORE_STATUS_OK;
}

/* ---------------- canonical state serialization ---------------- */
static void w_reserve(ByteWriter *w, size_t add)
{
    size_t need, cap;
    uint8_t *p;
    if(!w->ok) return;
    if(add > (size_t)-1 - w->size) { w->ok = 0; return; }
    need = w->size + add;
    if(need <= w->capacity) return;
    cap = w->capacity ? w->capacity : 4096u;
    while(cap < need) {
        if(cap > (size_t)-1 / 2u) { w->ok = 0; return; }
        cap *= 2u;
    }
    p = (uint8_t *)realloc(w->data, cap);
    if(p == NULL) { w->ok = 0; return; }
    w->data = p;
    w->capacity = cap;
}
static void w8(ByteWriter *w,uint8_t v){w_reserve(w,1u);if(w->ok)w->data[w->size++]=v;}
static void w16(ByteWriter *w,uint16_t v){w8(w,(uint8_t)v);w8(w,(uint8_t)(v>>8));}
static void w32(ByteWriter *w,uint32_t v){w16(w,(uint16_t)v);w16(w,(uint16_t)(v>>16));}
static void w64(ByteWriter *w,uint64_t v){w32(w,(uint32_t)v);w32(w,(uint32_t)(v>>32));}
static void wi16(ByteWriter *w,int16_t v){w16(w,(uint16_t)v);}
static void wbytes(ByteWriter *w,const void *p,size_t n){w_reserve(w,n);if(w->ok){memcpy(w->data+w->size,p,n);w->size+=n;}}
static uint8_t r8(ByteReader *r){if(!r->ok||r->pos>=r->size){r->ok=0;return 0;}return r->data[r->pos++];}
static uint16_t r16(ByteReader *r){uint16_t a=r8(r),b=r8(r);return (uint16_t)(a|(b<<8));}
static uint32_t r32(ByteReader *r){uint32_t a=r16(r),b=r16(r);return a|(b<<16);}
static uint64_t r64(ByteReader *r){uint64_t a=r32(r),b=r32(r);return a|(b<<32);}
static int16_t ri16(ByteReader *r){return (int16_t)r16(r);}
static void rbytes(ByteReader *r,void *p,size_t n){if(!r->ok||n>r->size-r->pos){r->ok=0;return;}memcpy(p,r->data+r->pos,n);r->pos+=n;}

static void save_mapper(ByteWriter*w,const Ai2Mmc3State*m){unsigned i;w8(w,m->bank_select);for(i=0;i<8u;i++)w8(w,m->bank_regs[i]);w8(w,m->bank_reg_known_mask);w8(w,m->bank_select_known);w8(w,m->ram_control);w8(w,m->ram_control_known);w8(w,(uint8_t)m->mirroring);w8(w,m->mirroring_known);w8(w,m->irq_latch);w8(w,m->irq_counter);w8(w,m->irq_latch_known);w8(w,m->irq_counter_known);w8(w,m->irq_reload_pending);w8(w,m->irq_reload_known);w8(w,m->irq_enabled);w8(w,m->irq_enabled_known);w8(w,m->irq_pending);w8(w,m->irq_pending_known);w8(w,m->ppu_a12_high);w8(w,m->ppu_a12_known);w8(w,m->a12_low_m2_falls);}
static void load_mapper(ByteReader*r,Ai2Mmc3State*m){unsigned i;m->bank_select=r8(r);for(i=0;i<8u;i++)m->bank_regs[i]=r8(r);m->bank_reg_known_mask=r8(r);m->bank_select_known=r8(r)!=0;m->ram_control=r8(r);m->ram_control_known=r8(r)!=0;m->mirroring=(Ai2NametableMirroring)r8(r);m->mirroring_known=r8(r)!=0;m->irq_latch=r8(r);m->irq_counter=r8(r);m->irq_latch_known=r8(r)!=0;m->irq_counter_known=r8(r)!=0;m->irq_reload_pending=r8(r)!=0;m->irq_reload_known=r8(r)!=0;m->irq_enabled=r8(r)!=0;m->irq_enabled_known=r8(r)!=0;m->irq_pending=r8(r)!=0;m->irq_pending_known=r8(r)!=0;m->ppu_a12_high=r8(r)!=0;m->ppu_a12_known=r8(r)!=0;m->a12_low_m2_falls=r8(r);}

static void save_runtime(ByteWriter*w,const Ai2V05Runtime*rt){unsigned i;w8(w,rt->a);w8(w,rt->x);w8(w,rt->y);w8(w,rt->s);w8(w,rt->p);wbytes(w,rt->ram,sizeof(rt->ram));w8(w,rt->open_bus);save_mapper(w,&rt->mapper);w64(w,rt->cpu_cycles);w64(w,rt->ppu_cycles);w64(w,rt->apu_cycles);w16(w,rt->current_pc);w8(w,rt->nmi_pending);w8(w,rt->nmi_poll);w8(w,rt->prev_nmi_poll);w8(w,rt->external_irq);w8(w,rt->irq_poll);w8(w,rt->prev_irq_poll);w8(w,rt->dma_draining);for(i=0;i<AI2_V06_STACK_DISPATCH_DEPTH;i++){w8(w,rt->stack_dispatch[i].bank);w16(w,rt->stack_dispatch[i].pc);}w8(w,rt->stack_dispatch_depth);w32(w,(uint32_t)rt->trap.code);w16(w,rt->trap.source_pc);w16(w,rt->trap.address);w8(w,rt->trap.value);w8(w,rt->trap.is_write);w8(w,rt->trap.physical_bank);w64(w,rt->trap.cpu_cycle);w64(w,rt->trap.ppu_cycle);w64(w,rt->trap.apu_cycle);}
static void load_runtime(ByteReader*r,Ai2V05Runtime*rt){unsigned i;rt->a=r8(r);rt->x=r8(r);rt->y=r8(r);rt->s=r8(r);rt->p=r8(r);rbytes(r,rt->ram,sizeof(rt->ram));rt->open_bus=r8(r);load_mapper(r,&rt->mapper);rt->cpu_cycles=r64(r);rt->ppu_cycles=r64(r);rt->apu_cycles=r64(r);rt->current_pc=r16(r);rt->nmi_pending=r8(r);rt->nmi_poll=r8(r);rt->prev_nmi_poll=r8(r);rt->external_irq=r8(r);rt->irq_poll=r8(r);rt->prev_irq_poll=r8(r);rt->dma_draining=r8(r);for(i=0;i<AI2_V06_STACK_DISPATCH_DEPTH;i++){rt->stack_dispatch[i].bank=r8(r);rt->stack_dispatch[i].pc=r16(r);}rt->stack_dispatch_depth=r8(r);rt->trap.code=(Ai2V06TrapCode)r32(r);rt->trap.source_pc=r16(r);rt->trap.address=r16(r);rt->trap.value=r8(r);rt->trap.is_write=r8(r);rt->trap.physical_bank=r8(r);rt->trap.cpu_cycle=r64(r);rt->trap.ppu_cycle=r64(r);rt->trap.apu_cycle=r64(r);}

static void save_sprite(ByteWriter*w,const Ai2V07Sprite*s){w8(w,s->y);w8(w,s->tile);w8(w,s->attr);w8(w,s->x);w8(w,s->pattern_lo);w8(w,s->pattern_hi);w8(w,s->oam_index);w8(w,s->valid);w8(w,s->row);}
static void load_sprite(ByteReader*r,Ai2V07Sprite*s){s->y=r8(r);s->tile=r8(r);s->attr=r8(r);s->x=r8(r);s->pattern_lo=r8(r);s->pattern_hi=r8(r);s->oam_index=r8(r);s->valid=r8(r);s->row=r8(r);}
static void save_ppu(ByteWriter*w,const Ai2V07Ppu*p){unsigned i;w8(w,p->ctrl);w8(w,p->mask);w8(w,p->status);w8(w,p->oam_addr);w16(w,p->v);w16(w,p->t);w8(w,p->fine_x);w8(w,p->write_toggle);w8(w,p->read_buffer);w8(w,p->ppu_open_bus);wbytes(w,p->ciram,sizeof(p->ciram));wbytes(w,p->palette,sizeof(p->palette));wbytes(w,p->oam,sizeof(p->oam));wbytes(w,p->secondary_oam,sizeof(p->secondary_oam));w16(w,p->bg_shift_lo);w16(w,p->bg_shift_hi);w16(w,p->at_shift_lo);w16(w,p->at_shift_hi);w8(w,p->next_tile);w8(w,p->next_attr);w8(w,p->next_bg_lo);w8(w,p->next_bg_hi);for(i=0;i<AI2_V07_SPRITES_PER_LINE;i++)save_sprite(w,&p->sprites[i]);for(i=0;i<AI2_V07_SPRITES_PER_LINE;i++)save_sprite(w,&p->next_sprites[i]);w8(w,p->sprite_count);w8(w,p->next_sprite_count);w8(w,p->sprite_zero_current);w8(w,p->sprite_zero_next);for(i=0;i<AI2_V07_SCREEN_PIXELS;i++)w16(w,p->frame_indexed[i]);w64(w,p->frame_generation);w64(w,p->frame_count);wi16(w,p->scanline);w16(w,p->dot);w8(w,p->suppress_vblank);w8(w,p->nmi_line);w16(w,p->last_bus_address);w64(w,p->ppu_memory_reads);w64(w,p->ppu_memory_writes);w64(w,p->mapper_a12_clocks);w64(w,p->mapper_a12_ambiguous_while_irq_disabled);w32(w,(uint32_t)p->error);}
static void load_ppu(ByteReader*r,Ai2V07Ppu*p){unsigned i;p->ctrl=r8(r);p->mask=r8(r);p->status=r8(r);p->oam_addr=r8(r);p->v=r16(r);p->t=r16(r);p->fine_x=r8(r);p->write_toggle=r8(r);p->read_buffer=r8(r);p->ppu_open_bus=r8(r);rbytes(r,p->ciram,sizeof(p->ciram));rbytes(r,p->palette,sizeof(p->palette));rbytes(r,p->oam,sizeof(p->oam));rbytes(r,p->secondary_oam,sizeof(p->secondary_oam));p->bg_shift_lo=r16(r);p->bg_shift_hi=r16(r);p->at_shift_lo=r16(r);p->at_shift_hi=r16(r);p->next_tile=r8(r);p->next_attr=r8(r);p->next_bg_lo=r8(r);p->next_bg_hi=r8(r);for(i=0;i<AI2_V07_SPRITES_PER_LINE;i++)load_sprite(r,&p->sprites[i]);for(i=0;i<AI2_V07_SPRITES_PER_LINE;i++)load_sprite(r,&p->next_sprites[i]);p->sprite_count=r8(r);p->next_sprite_count=r8(r);p->sprite_zero_current=r8(r);p->sprite_zero_next=r8(r);for(i=0;i<AI2_V07_SCREEN_PIXELS;i++)p->frame_indexed[i]=r16(r);p->frame_generation=r64(r);p->frame_count=r64(r);p->scanline=ri16(r);p->dot=r16(r);p->suppress_vblank=r8(r);p->nmi_line=r8(r);p->last_bus_address=r16(r);p->ppu_memory_reads=r64(r);p->ppu_memory_writes=r64(r);p->mapper_a12_clocks=r64(r);p->mapper_a12_ambiguous_while_irq_disabled=r64(r);p->error=(Ai2V07PpuError)r32(r);}

static void save_len(ByteWriter*w,const Ai2V08LengthCounter*x){w8(w,x->enabled);w8(w,x->halt);w8(w,x->counter);}static void load_len(ByteReader*r,Ai2V08LengthCounter*x){x->enabled=r8(r);x->halt=r8(r);x->counter=r8(r);}
static void save_env(ByteWriter*w,const Ai2V08Envelope*x){save_len(w,&x->length);w8(w,x->constant_volume);w8(w,x->volume_period);w8(w,x->start);w8(w,x->divider);w8(w,x->decay);}static void load_env(ByteReader*r,Ai2V08Envelope*x){load_len(r,&x->length);x->constant_volume=r8(r);x->volume_period=r8(r);x->start=r8(r);x->divider=r8(r);x->decay=r8(r);}
static void save_pulse(ByteWriter*w,const Ai2V08Pulse*x){save_env(w,&x->envelope);w16(w,x->period);w16(w,x->timer);w8(w,x->duty);w8(w,x->duty_pos);w8(w,x->sweep_enabled);w8(w,x->sweep_period);w8(w,x->sweep_negate);w8(w,x->sweep_shift);w8(w,x->sweep_reload);w8(w,x->sweep_divider);w8(w,x->channel1);w8(w,x->output);}static void load_pulse(ByteReader*r,Ai2V08Pulse*x){load_env(r,&x->envelope);x->period=r16(r);x->timer=r16(r);x->duty=r8(r);x->duty_pos=r8(r);x->sweep_enabled=r8(r);x->sweep_period=r8(r);x->sweep_negate=r8(r);x->sweep_shift=r8(r);x->sweep_reload=r8(r);x->sweep_divider=r8(r);x->channel1=r8(r);x->output=r8(r);}
static void save_tri(ByteWriter*w,const Ai2V08Triangle*x){save_len(w,&x->length);w16(w,x->period);w16(w,x->timer);w8(w,x->sequence_pos);w8(w,x->control);w8(w,x->linear_reload_value);w8(w,x->linear_counter);w8(w,x->linear_reload_flag);w8(w,x->output);}static void load_tri(ByteReader*r,Ai2V08Triangle*x){load_len(r,&x->length);x->period=r16(r);x->timer=r16(r);x->sequence_pos=r8(r);x->control=r8(r);x->linear_reload_value=r8(r);x->linear_counter=r8(r);x->linear_reload_flag=r8(r);x->output=r8(r);}
static void save_noise(ByteWriter*w,const Ai2V08Noise*x){save_env(w,&x->envelope);w16(w,x->timer_period);w16(w,x->timer);w16(w,x->lfsr);w8(w,x->mode);w8(w,x->output);}static void load_noise(ByteReader*r,Ai2V08Noise*x){load_env(r,&x->envelope);x->timer_period=r16(r);x->timer=r16(r);x->lfsr=r16(r);x->mode=r8(r);x->output=r8(r);}
static void save_dmc(ByteWriter*w,const Ai2V08Dmc*x){w16(w,x->period);w16(w,x->timer);w16(w,x->sample_address);w16(w,x->sample_length);w16(w,x->current_address);w16(w,x->bytes_remaining);w8(w,x->output_level);w8(w,x->irq_enabled);w8(w,x->loop);w8(w,x->irq_flag);w8(w,x->sample_buffer);w8(w,x->sample_buffer_empty);w8(w,x->shift_register);w8(w,x->bits_remaining);w8(w,x->silence);w8(w,x->enabled);w8(w,x->dma_pending);}static void load_dmc(ByteReader*r,Ai2V08Dmc*x){x->period=r16(r);x->timer=r16(r);x->sample_address=r16(r);x->sample_length=r16(r);x->current_address=r16(r);x->bytes_remaining=r16(r);x->output_level=r8(r);x->irq_enabled=r8(r);x->loop=r8(r);x->irq_flag=r8(r);x->sample_buffer=r8(r);x->sample_buffer_empty=r8(r);x->shift_register=r8(r);x->bits_remaining=r8(r);x->silence=r8(r);x->enabled=r8(r);x->dma_pending=r8(r);}
static void save_controller(ByteWriter*w,const Ai2V08Controller*x){w8(w,x->live);w8(w,x->latched);w8(w,x->shift);}static void load_controller(ByteReader*r,Ai2V08Controller*x){x->live=r8(r);x->latched=r8(r);x->shift=r8(r);}
static void save_apu(ByteWriter*w,const Ai2V08Apu*a){unsigned i;save_pulse(w,&a->pulse1);save_pulse(w,&a->pulse2);save_tri(w,&a->triangle);save_noise(w,&a->noise);save_dmc(w,&a->dmc);save_controller(w,&a->controllers[0]);save_controller(w,&a->controllers[1]);w64(w,a->cycle);w32(w,a->frame_cycle);w8(w,a->frame_mode5);w8(w,a->frame_irq_inhibit);w8(w,a->frame_irq_flag);w8(w,(uint8_t)a->frame_write_delay);w8(w,a->frame_pending_value);w8(w,a->controller_strobe);w8(w,a->oam_dma_pending);w8(w,a->oam_dma_page);w64(w,a->oam_dma_count);w64(w,a->oam_dma_cycles);w64(w,a->dmc_dma_count);w64(w,a->dmc_dma_cycles);w64(w,a->dma_collision_count);w64(w,a->quarter_frame_ticks);w64(w,a->half_frame_ticks);w64(w,a->register_reads);w64(w,a->register_writes);w64(w,a->controller_reads);w64(w,a->controller_latches);w32(w,a->pcm_phase);for(i=0;i<AI2_V08_PCM_CAPACITY;i++)wi16(w,a->pcm[i]);w32(w,(uint32_t)a->pcm_read);w32(w,(uint32_t)a->pcm_write);w32(w,(uint32_t)a->pcm_count);w64(w,a->pcm_total_samples);w64(w,a->pcm_dropped);}
static void load_apu(ByteReader*r,Ai2V08Apu*a){unsigned i;load_pulse(r,&a->pulse1);load_pulse(r,&a->pulse2);load_tri(r,&a->triangle);load_noise(r,&a->noise);load_dmc(r,&a->dmc);load_controller(r,&a->controllers[0]);load_controller(r,&a->controllers[1]);a->cycle=r64(r);a->frame_cycle=r32(r);a->frame_mode5=r8(r);a->frame_irq_inhibit=r8(r);a->frame_irq_flag=r8(r);a->frame_write_delay=(int8_t)r8(r);a->frame_pending_value=r8(r);a->controller_strobe=r8(r);a->oam_dma_pending=r8(r);a->oam_dma_page=r8(r);a->oam_dma_count=r64(r);a->oam_dma_cycles=r64(r);a->dmc_dma_count=r64(r);a->dmc_dma_cycles=r64(r);a->dma_collision_count=r64(r);a->quarter_frame_ticks=r64(r);a->half_frame_ticks=r64(r);a->register_reads=r64(r);a->register_writes=r64(r);a->controller_reads=r64(r);a->controller_latches=r64(r);a->pcm_phase=r32(r);for(i=0;i<AI2_V08_PCM_CAPACITY;i++)a->pcm[i]=ri16(r);a->pcm_read=(size_t)r32(r);a->pcm_write=(size_t)r32(r);a->pcm_count=(size_t)r32(r);a->pcm_total_samples=r64(r);a->pcm_dropped=r64(r);}

static int serialize_state(const Ai2StaticCore *core, uint8_t **data, size_t *size)
{
    ByteWriter w;
    memset(&w,0,sizeof(w));w.ok=1;
    w32(&w,AI2_STATE_VERSION);
    w8(&w,core->current.bank);w16(&w,core->current.pc);
    w8(&w,core->pending_buttons[0]);w8(&w,core->pending_buttons[1]);
    w64(&w,core->executed_instructions);
    w32(&w,0u); /* executable-RAM epoch count: V09 authority has no RAM code epochs */
    save_runtime(&w,&core->runtime);
    save_ppu(&w,&core->ppu);
    save_apu(&w,&core->devices.apu);
    if(!w.ok){free(w.data);return 0;}
    *data=w.data;*size=w.size;return 1;
}

static int validate_loaded_state(Ai2StaticCore *core)
{
    uint8_t mapped = 0xFFu;
    if(core->current.bank >= AI2_PRG_BANK_COUNT) return 0;
    if(!ai2_v05_generated_identity_exists(core->current.bank, core->current.pc)) return 0;
    if(ai2_v05_runtime_map_prg_identity(&core->runtime, core->current.pc, &mapped) != 0 || mapped != core->current.bank) return 0;
    if(core->runtime.stack_dispatch_depth > AI2_V06_STACK_DISPATCH_DEPTH) return 0;
    if(core->runtime.ppu_cycles != core->runtime.cpu_cycles * 3u || core->runtime.apu_cycles != core->runtime.cpu_cycles) return 0;
    if(core->ppu.scanline < 0 || core->ppu.scanline >= AI2_V07_NTSC_SCANLINES || core->ppu.dot >= AI2_V07_DOTS_PER_SCANLINE) return 0;
    if(core->ppu.sprite_count > AI2_V07_SPRITES_PER_LINE || core->ppu.next_sprite_count > AI2_V07_SPRITES_PER_LINE) return 0;
    if(core->ppu.error != AI2_V07_PPU_OK) return 0;
    if(core->devices.apu.pcm_read >= AI2_V08_PCM_CAPACITY || core->devices.apu.pcm_write >= AI2_V08_PCM_CAPACITY || core->devices.apu.pcm_count > AI2_V08_PCM_CAPACITY) return 0;
    if(core->devices.apu.controllers[0].live != core->pending_buttons[0] || core->devices.apu.controllers[1].live != core->pending_buttons[1]) return 0;
    if(core->runtime.trap.code != AI2_V06_TRAP_NONE) return 0;
    return 1;
}

static int deserialize_state(Ai2StaticCore *core, const uint8_t *data, size_t size)
{
    ByteReader r;uint32_t version,ram_epochs;
    memset(&r,0,sizeof(r));r.data=data;r.size=size;r.ok=1;
    version=r32(&r);if(version!=AI2_STATE_VERSION)return 0;
    core->current.bank=r8(&r);core->current.pc=r16(&r);
    core->pending_buttons[0]=r8(&r);core->pending_buttons[1]=r8(&r);
    core->executed_instructions=r64(&r);
    ram_epochs=r32(&r);if(ram_epochs!=0u)return 0;
    load_runtime(&r,&core->runtime);
    load_ppu(&r,&core->ppu);
    load_apu(&r,&core->devices.apu);
    if(!r.ok||r.pos!=r.size)return 0;
    core_bind_machine(core);
    core_bind_observer(core);
    core->stop_requested=0u;core->stop_after_instruction=0u;core->stopped=0u;core->status=AI2_CORE_STATUS_OK;
    return validate_loaded_state(core);
}

static int copy_canonical_state(Ai2StaticCore *dst, const Ai2StaticCore *src)
{
    dst->runtime = src->runtime;
    dst->ppu = src->ppu;
    dst->devices.apu = src->devices.apu;
    dst->current = src->current;
    dst->pending_buttons[0] = src->pending_buttons[0];
    dst->pending_buttons[1] = src->pending_buttons[1];
    dst->executed_instructions = src->executed_instructions;
    dst->stop_requested = 0u;
    dst->stop_after_instruction = 0u;
    dst->stopped = 0u;
    dst->status = AI2_CORE_STATUS_OK;
    core_bind_machine(dst);
    core_bind_observer(dst);
    return validate_loaded_state(dst);
}

static int write_u32_file(FILE *f,uint32_t v){uint8_t b[4]={(uint8_t)v,(uint8_t)(v>>8),(uint8_t)(v>>16),(uint8_t)(v>>24)};return fwrite(b,1,4,f)==4;}
static int write_u64_file(FILE *f,uint64_t v){return write_u32_file(f,(uint32_t)v)&&write_u32_file(f,(uint32_t)(v>>32));}

#if defined(_WIN32)
static wchar_t *wide_from_utf8(const char *path)
{
    int count;wchar_t *wide;
    if(path==NULL||path[0]=='\0')return NULL;
    count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,NULL,0);if(count<=0)return NULL;
    wide=(wchar_t*)malloc((size_t)count*sizeof(*wide));if(!wide)return NULL;
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,count)){free(wide);return NULL;}
    return wide;
}

static FILE *open_utf8_file(const char *path,const wchar_t *mode)
{
    wchar_t *wide=wide_from_utf8(path);FILE *file=NULL;
    if(!wide)return NULL;
    if(_wfopen_s(&file,wide,mode)!=0)file=NULL;
    free(wide);return file;
}

static int remove_utf8_file(const char *path)
{
    wchar_t *wide=wide_from_utf8(path);int result;
    if(!wide)return -1;result=_wremove(wide);free(wide);return result;
}
#else
static FILE *open_utf8_file(const char *path,const char *mode){return fopen(path,mode);}
static int remove_utf8_file(const char *path){return remove(path);}
#endif

static int read_whole_file(const char *path, uint8_t **data, size_t *size)
{
    FILE *f; long n; uint8_t *p;
    *data=NULL;*size=0u;
#if defined(_WIN32)
    f=open_utf8_file(path,L"rb");if(!f)return 0;
#else
    f=open_utf8_file(path,"rb");if(!f)return 0;
#endif
    if(fseek(f,0,SEEK_END)!=0||(n=ftell(f))<0||fseek(f,0,SEEK_SET)!=0){fclose(f);return 0;}
    if((unsigned long)n>AI2_SNAPSHOT_MAX_BYTES){fclose(f);return 0;}
    p=(uint8_t*)malloc((size_t)n? (size_t)n:1u);if(!p){fclose(f);return 0;}
    if((size_t)n&&fread(p,1,(size_t)n,f)!=(size_t)n){free(p);fclose(f);return 0;}
    if(fclose(f)!=0){free(p);return 0;}
    *data=p;*size=(size_t)n;return 1;
}

static uint32_t mem_u32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint64_t mem_u64(const uint8_t *p){return (uint64_t)mem_u32(p)|((uint64_t)mem_u32(p+4)<<32);}

static Ai2SnapshotStatus parse_snapshot_bytes(const Ai2StaticCore *core,
                                              const uint8_t *file,size_t file_size,
                                              const uint8_t **payload,size_t *payload_size,
                                              char *error,size_t error_capacity)
{
    const size_t header=8u+4u+4u+32u+32u+8u+32u;
    uint32_t version,header_size;uint64_t n;uint8_t digest[32];
    if(file==NULL||file_size<header){set_error(error,error_capacity,"snapshot header is truncated");return AI2_SNAPSHOT_BAD_FORMAT;}
    if(memcmp(file,k_snapshot_magic,8u)!=0){set_error(error,error_capacity,"snapshot magic does not match");return AI2_SNAPSHOT_BAD_FORMAT;}
    version=mem_u32(file+8u);header_size=mem_u32(file+12u);
    if(version!=AI2_SNAPSHOT_VERSION){set_error(error,error_capacity,"snapshot version is unsupported");return AI2_SNAPSHOT_UNSUPPORTED_VERSION;}
    if(header_size!=header){set_error(error,error_capacity,"snapshot header size is invalid");return AI2_SNAPSHOT_BAD_FORMAT;}
    if(!digest_equal(file+16u,AI2_V09_CORE_COMPAT_DIGEST)){set_error(error,error_capacity,"snapshot core compatibility identity differs");return AI2_SNAPSHOT_CORE_MISMATCH;}
    if(core==NULL||!core->loaded||!digest_equal(file+48u,core->rom_digest)){set_error(error,error_capacity,"snapshot ROM identity differs");return AI2_SNAPSHOT_ROM_MISMATCH;}
    n=mem_u64(file+80u);
    if(n>(uint64_t)(file_size-header)){set_error(error,error_capacity,"snapshot payload length is invalid");return AI2_SNAPSHOT_BAD_FORMAT;}
    if((uint64_t)(file_size-header)!=n){set_error(error,error_capacity,"snapshot contains trailing data");return AI2_SNAPSHOT_TRAILING_DATA;}
    ai2_sha256(file+header,(size_t)n,digest);
    if(!digest_equal(digest,file+88u)){set_error(error,error_capacity,"snapshot payload hash mismatch");return AI2_SNAPSHOT_PAYLOAD_HASH_MISMATCH;}
    *payload=file+header;*payload_size=(size_t)n;return AI2_SNAPSHOT_OK;
}

static int atomic_replace(const char *tmp,const char *path)
{
#if defined(_WIN32)
    wchar_t *wide_tmp=wide_from_utf8(tmp),*wide_path=wide_from_utf8(path);int result=0;
    if(wide_tmp&&wide_path)result=MoveFileExW(wide_tmp,wide_path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    free(wide_tmp);free(wide_path);return result;
#else
    return rename(tmp,path)==0;
#endif
}

Ai2SnapshotStatus ai2_static_core_snapshot_save(const Ai2StaticCore *core,
                                                const char *utf8_path,
                                                char *error,
                                                size_t error_capacity)
{
    uint8_t *payload=NULL,*check_file=NULL;size_t payload_size=0u,check_size=0u;uint8_t payload_hash[32];
    char *tmp=NULL;size_t path_len;FILE *f=NULL;Ai2SnapshotStatus status;const uint8_t *check_payload;size_t check_payload_size;
    const uint32_t header_size=(uint32_t)(8u+4u+4u+32u+32u+8u+32u);
    if(core==NULL||utf8_path==NULL||utf8_path[0]=='\0'){set_error(error,error_capacity,"invalid snapshot argument");return AI2_SNAPSHOT_INVALID_ARGUMENT;}
    if(!core_safe_boundary(core)){set_error(error,error_capacity,"snapshot save requires an instruction-safe external boundary");return AI2_SNAPSHOT_NOT_SAFE_BOUNDARY;}
    if(!serialize_state(core,&payload,&payload_size)){set_error(error,error_capacity,"unable to allocate canonical snapshot state");return AI2_SNAPSHOT_NO_MEMORY;}
    ai2_sha256(payload,payload_size,payload_hash);
    path_len=strlen(utf8_path);tmp=(char*)malloc(path_len+64u);if(!tmp){free(payload);return AI2_SNAPSHOT_NO_MEMORY;}
    snprintf(tmp,path_len+64u,"%s.tmp.%ld",utf8_path,(long)AI2_GETPID());
#if defined(_WIN32)
    f=open_utf8_file(tmp,L"wb");
#else
    f=open_utf8_file(tmp,"wb");
#endif
    if(!f){set_error(error,error_capacity,"snapshot temporary file could not be opened");free(tmp);free(payload);return AI2_SNAPSHOT_IO_ERROR;}
    {
        int write_ok = fwrite(k_snapshot_magic,1,sizeof(k_snapshot_magic),f)==sizeof(k_snapshot_magic) &&
            write_u32_file(f,AI2_SNAPSHOT_VERSION) && write_u32_file(f,header_size) &&
            fwrite(AI2_V09_CORE_COMPAT_DIGEST,1,32u,f)==32u && fwrite(core->rom_digest,1,32u,f)==32u &&
            write_u64_file(f,(uint64_t)payload_size) && fwrite(payload_hash,1,32u,f)==32u &&
            fwrite(payload,1,payload_size,f)==payload_size && fflush(f)==0 && AI2_FSYNC(AI2_FILENO(f))==0;
        int close_ok = fclose(f)==0;
        f=NULL;
        if(!write_ok || !close_ok){remove_utf8_file(tmp);set_error(error,error_capacity,"snapshot temporary write/flush failed");free(tmp);free(payload);return AI2_SNAPSHOT_IO_ERROR;}
    }
    if(!read_whole_file(tmp,&check_file,&check_size)){remove_utf8_file(tmp);set_error(error,error_capacity,"snapshot temporary validation read failed");free(tmp);free(payload);return AI2_SNAPSHOT_IO_ERROR;}
    status=parse_snapshot_bytes(core,check_file,check_size,&check_payload,&check_payload_size,error,error_capacity);
    if(status!=AI2_SNAPSHOT_OK||check_payload_size!=payload_size||memcmp(check_payload,payload,payload_size)!=0){free(check_file);remove_utf8_file(tmp);free(tmp);free(payload);if(status==AI2_SNAPSHOT_OK){set_error(error,error_capacity,"snapshot temporary file differs from canonical payload");status=AI2_SNAPSHOT_BAD_FORMAT;}return status;}
    free(check_file);
    if(!atomic_replace(tmp,utf8_path)){remove_utf8_file(tmp);set_error(error,error_capacity,"atomic snapshot replace failed");free(tmp);free(payload);return AI2_SNAPSHOT_IO_ERROR;}
    free(tmp);free(payload);set_error(error,error_capacity,"");return AI2_SNAPSHOT_OK;
}

Ai2SnapshotStatus ai2_static_core_snapshot_load(Ai2StaticCore *core,
                                                const char *utf8_path,
                                                char *error,
                                                size_t error_capacity)
{
    uint8_t *file=NULL;size_t file_size=0u;const uint8_t *payload=NULL;size_t payload_size=0u;Ai2SnapshotStatus status;Ai2StaticCore *temp;
    if(core==NULL||utf8_path==NULL||utf8_path[0]=='\0'){set_error(error,error_capacity,"invalid snapshot argument");return AI2_SNAPSHOT_INVALID_ARGUMENT;}
    if(!core_safe_boundary(core)){set_error(error,error_capacity,"snapshot load requires an instruction-safe external boundary");return AI2_SNAPSHOT_NOT_SAFE_BOUNDARY;}
    if(!read_whole_file(utf8_path,&file,&file_size)){set_error(error,error_capacity,"snapshot could not be read");return AI2_SNAPSHOT_IO_ERROR;}
    status=parse_snapshot_bytes(core,file,file_size,&payload,&payload_size,error,error_capacity);if(status!=AI2_SNAPSHOT_OK){free(file);return status;}
    temp=ai2_static_core_create();if(!temp){free(file);return AI2_SNAPSHOT_NO_MEMORY;}
    memcpy(temp->prg_rom,core->prg_rom,sizeof(temp->prg_rom));memcpy(temp->chr_rom,core->chr_rom,sizeof(temp->chr_rom));memcpy(temp->rom_digest,core->rom_digest,32u);temp->loaded=1u;temp->pending_buttons[0]=0u;temp->pending_buttons[1]=0u;
    if(!core_reset_machine(temp)||!deserialize_state(temp,payload,payload_size)){ai2_static_core_destroy(temp);free(file);set_error(error,error_capacity,"snapshot state failed canonical validation");return AI2_SNAPSHOT_INVALID_STATE;}
    if(!copy_canonical_state(core,temp)){ai2_static_core_destroy(temp);free(file);set_error(error,error_capacity,"snapshot state commit validation failed");return AI2_SNAPSHOT_INVALID_STATE;}
    ai2_static_core_destroy(temp);free(file);set_error(error,error_capacity,"");return AI2_SNAPSHOT_OK;
}

const char *ai2_snapshot_status_name(Ai2SnapshotStatus status)
{
    switch(status){case AI2_SNAPSHOT_OK:return "ok";case AI2_SNAPSHOT_INVALID_ARGUMENT:return "invalid_argument";case AI2_SNAPSHOT_NO_MEMORY:return "no_memory";case AI2_SNAPSHOT_IO_ERROR:return "io_error";case AI2_SNAPSHOT_BAD_FORMAT:return "bad_format";case AI2_SNAPSHOT_UNSUPPORTED_VERSION:return "unsupported_version";case AI2_SNAPSHOT_ROM_MISMATCH:return "rom_mismatch";case AI2_SNAPSHOT_CORE_MISMATCH:return "core_mismatch";case AI2_SNAPSHOT_PAYLOAD_HASH_MISMATCH:return "payload_hash_mismatch";case AI2_SNAPSHOT_TRAILING_DATA:return "trailing_data";case AI2_SNAPSHOT_INVALID_STATE:return "invalid_state";case AI2_SNAPSHOT_NOT_SAFE_BOUNDARY:return "not_safe_boundary";default:return "unknown";}
}

const char *ai2_static_core_rom_sha256(void){return AI2_V09_ROM_SHA256;}
const char *ai2_static_core_compatibility_sha256(void){return AI2_V09_CORE_COMPAT_SHA256;}
