#ifndef AI2_V06_RUNTIME_H
#define AI2_V06_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "ai2_v05_generated.h"
#include "ai2_mmc3_mapper.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    AI2_V06_FLAG_C = 0x01,
    AI2_V06_FLAG_Z = 0x02,
    AI2_V06_FLAG_I = 0x04,
    AI2_V06_FLAG_D = 0x08,
    AI2_V06_FLAG_B = 0x10,
    AI2_V06_FLAG_U = 0x20,
    AI2_V06_FLAG_V = 0x40,
    AI2_V06_FLAG_N = 0x80,
    AI2_V06_RAM_BYTES = 0x800,
    AI2_V06_STACK_DISPATCH_DEPTH = 16
};

typedef enum Ai2V06TrapCode {
    AI2_V06_TRAP_NONE = 0,
    AI2_V06_TRAP_NULL,
    AI2_V06_TRAP_ROM_GEOMETRY,
    AI2_V06_TRAP_UNRESOLVED_MAPPER,
    AI2_V06_TRAP_BAD_PRG_MAPPING,
    AI2_V06_TRAP_PPU_NOT_CONSTRUCTED,
    AI2_V06_TRAP_APU_IO_NOT_CONSTRUCTED,
    AI2_V06_TRAP_DMA_NOT_CONSTRUCTED,
    AI2_V06_TRAP_DEVICE_REJECTED,
    AI2_V06_TRAP_BAD_GENERATED_IDENTITY,
    AI2_V06_TRAP_GENERATED_DISPATCH,
    AI2_V06_TRAP_STACK_DISPATCH_OVERFLOW,
    AI2_V06_TRAP_STACK_DISPATCH_UNDERFLOW,
    AI2_V06_TRAP_BAD_INTERRUPT_TARGET,
    AI2_V06_TRAP_INTERNAL_CONTRACT
} Ai2V06TrapCode;

typedef enum Ai2V06BusAccessKind {
    AI2_V06_BUS_CODE_CYCLE = 0,
    AI2_V06_BUS_OPERAND_CYCLE,
    AI2_V06_BUS_READ,
    AI2_V06_BUS_WRITE,
    AI2_V06_BUS_DUMMY_READ,
    AI2_V06_BUS_RMW_WRITE_OLD,
    AI2_V06_BUS_RMW_WRITE_NEW,
    AI2_V06_BUS_STACK_READ,
    AI2_V06_BUS_STACK_WRITE,
    AI2_V06_BUS_VECTOR_READ,
    AI2_V06_BUS_INTERNAL_CYCLE
} Ai2V06BusAccessKind;

typedef struct Ai2V06Trap {
    Ai2V06TrapCode code;
    uint16_t source_pc;
    uint16_t address;
    uint8_t value;
    uint8_t is_write;
    uint8_t physical_bank;
    uint64_t cpu_cycle;
    uint64_t ppu_cycle;
    uint64_t apu_cycle;
} Ai2V06Trap;

typedef struct Ai2V06BusEvent {
    Ai2V06BusAccessKind kind;
    uint16_t address;
    uint8_t value;
    uint8_t is_write;
    uint64_t cpu_cycle;
} Ai2V06BusEvent;

struct Ai2V05Runtime;
typedef int (*Ai2V06DeviceReadFn)(struct Ai2V05Runtime *rt, void *user, uint16_t address, uint8_t *value);
typedef int (*Ai2V06DeviceWriteFn)(struct Ai2V05Runtime *rt, void *user, uint16_t address, uint8_t value);
typedef void (*Ai2V06DeviceTickFn)(struct Ai2V05Runtime *rt, void *user);
typedef void (*Ai2V06DeviceResetFn)(struct Ai2V05Runtime *rt, void *user, int soft_reset);
typedef void (*Ai2V06DeviceDmaDrainFn)(struct Ai2V05Runtime *rt, void *user);
typedef void (*Ai2V06BusObserverFn)(void *user, const Ai2V06BusEvent *event);

typedef struct Ai2V06DeviceHooks {
    void *user;
    Ai2V06DeviceReadFn ppu_read;
    Ai2V06DeviceWriteFn ppu_write;
    Ai2V06DeviceReadFn apu_io_read;
    Ai2V06DeviceWriteFn apu_io_write;
    Ai2V06DeviceWriteFn oam_dma_start;
    Ai2V06DeviceTickFn ppu_tick;
    Ai2V06DeviceTickFn apu_tick;
    Ai2V06DeviceTickFn dma_tick;
    Ai2V06DeviceResetFn reset;
    Ai2V06DeviceDmaDrainFn dma_drain;
} Ai2V06DeviceHooks;

typedef struct Ai2V06StackDispatchOrigin {
    uint8_t bank;
    uint16_t pc;
} Ai2V06StackDispatchOrigin;

/*
 * V06 is the owner of the concrete runtime behind the V05 opaque API.  PPU and
 * APU/DMA callbacks are construction boundaries for V07/V08; missing callbacks
 * trap instead of returning fabricated values.
 */
typedef struct Ai2V05Runtime {
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t s;
    uint8_t p;

    uint8_t ram[AI2_V06_RAM_BYTES];
    uint8_t open_bus;

    const uint8_t *prg_rom;
    size_t prg_rom_size;
    const uint8_t *chr_rom;
    size_t chr_rom_size;

    Ai2Mmc3State mapper;

    uint64_t cpu_cycles;
    uint64_t ppu_cycles;
    uint64_t apu_cycles;

    uint16_t current_pc;
    uint8_t nmi_pending;
    uint8_t nmi_poll;
    uint8_t prev_nmi_poll;
    uint8_t external_irq;
    uint8_t irq_poll;
    uint8_t prev_irq_poll;
    uint8_t dma_draining;

    Ai2V06DeviceHooks devices;
    Ai2V06BusObserverFn bus_observer;
    void *bus_observer_user;

    Ai2V06StackDispatchOrigin stack_dispatch[AI2_V06_STACK_DISPATCH_DEPTH];
    uint8_t stack_dispatch_depth;

    Ai2V06Trap trap;
} Ai2V05Runtime;

void ai2_v06_init(Ai2V05Runtime *rt, const uint8_t *prg_rom, size_t prg_rom_size,
                  const uint8_t *chr_rom, size_t chr_rom_size, uint8_t ram_fill);
void ai2_v06_set_devices(Ai2V05Runtime *rt, const Ai2V06DeviceHooks *hooks);
void ai2_v06_set_bus_observer(Ai2V05Runtime *rt, Ai2V06BusObserverFn observer, void *user);

int ai2_v06_reset(Ai2V05Runtime *rt, Ai2V05Identity *entry);
int ai2_v06_step(Ai2V05Runtime *rt, Ai2V05Identity current, Ai2V05Identity *next);
void ai2_v06_request_nmi(Ai2V05Runtime *rt);
void ai2_v06_clear_nmi(Ai2V05Runtime *rt);
void ai2_v06_set_external_irq(Ai2V05Runtime *rt, int asserted);
void ai2_v06_raise_device_trap(Ai2V05Runtime *rt, Ai2V06TrapCode code, uint16_t address, uint8_t value, int is_write);

int ai2_v06_bus_read(Ai2V05Runtime *rt, uint16_t address, Ai2V06BusAccessKind kind, uint8_t *value);
int ai2_v06_bus_write(Ai2V05Runtime *rt, uint16_t address, uint8_t value, Ai2V06BusAccessKind kind);
void ai2_v06_code_cycle(Ai2V05Runtime *rt, uint16_t source_pc, uint8_t static_byte, Ai2V06BusAccessKind kind);
void ai2_v06_internal_cycle(Ai2V05Runtime *rt);

const Ai2V06Trap *ai2_v06_get_trap(const Ai2V05Runtime *rt);
void ai2_v06_clear_trap(Ai2V05Runtime *rt);
int ai2_v06_has_trap(const Ai2V05Runtime *rt);

#ifdef __cplusplus
}
#endif

#endif
