#ifndef AI2_STATIC_CORE_H
#define AI2_STATIC_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    AI2_CORE_FRAME_WIDTH = 256,
    AI2_CORE_FRAME_HEIGHT = 240,
    AI2_CORE_FRAME_PIXELS = AI2_CORE_FRAME_WIDTH * AI2_CORE_FRAME_HEIGHT,
    AI2_CORE_DEFAULT_FRAME_INSTRUCTION_LIMIT = 100000
};

typedef struct Ai2StaticCore Ai2StaticCore;

typedef enum Ai2CoreStatus {
    AI2_CORE_STATUS_OK = 0,
    AI2_CORE_STATUS_INVALID_ARGUMENT,
    AI2_CORE_STATUS_NO_MEMORY,
    AI2_CORE_STATUS_BAD_ROM,
    AI2_CORE_STATUS_NOT_LOADED,
    AI2_CORE_STATUS_REENTRANT_CALL,
    AI2_CORE_STATUS_NOT_SAFE_BOUNDARY,
    AI2_CORE_STATUS_IO_ERROR,
    AI2_CORE_STATUS_BAD_SNAPSHOT,
    AI2_CORE_STATUS_SNAPSHOT_VERSION,
    AI2_CORE_STATUS_ROM_MISMATCH,
    AI2_CORE_STATUS_CORE_MISMATCH,
    AI2_CORE_STATUS_SNAPSHOT_HASH_MISMATCH,
    AI2_CORE_STATUS_INVALID_STATE,
    AI2_CORE_STATUS_STEP_LIMIT
} Ai2CoreStatus;

typedef struct Ai2CoreTrapInfo {
    uint32_t runtime_trap_code;
    uint16_t source_pc;
    uint16_t address;
    uint8_t value;
    uint8_t is_write;
    uint8_t physical_prg_bank;
    uint8_t reserved;
    uint64_t cpu_cycle;
    uint64_t ppu_cycle;
    uint64_t apu_cycle;
} Ai2CoreTrapInfo;

typedef struct Ai2FrameResult {
    uint8_t completed;
    uint8_t stopped;
    uint8_t player1_buttons;
    uint8_t player2_buttons;
    uint8_t physical_prg_bank;
    uint8_t reserved[3];
    uint16_t program_counter;
    uint16_t reserved2;
    uint64_t start_frame;
    uint64_t end_frame;
    uint64_t executed_instructions;
    Ai2CoreTrapInfo trap;
} Ai2FrameResult;

typedef enum Ai2RuntimeEventType {
    AI2_RUNTIME_EVENT_INSTRUCTION_BEFORE = 1u << 0,
    AI2_RUNTIME_EVENT_BUS_READ = 1u << 1,
    AI2_RUNTIME_EVENT_BUS_WRITE = 1u << 2,
    AI2_RUNTIME_EVENT_INSTRUCTION_AFTER = 1u << 3,
    AI2_RUNTIME_EVENT_FRAME_COMPLETE = 1u << 4,
    AI2_RUNTIME_EVENT_FRONTIER = 1u << 5,
    AI2_RUNTIME_EVENT_ALL = (1u << 6) - 1u
} Ai2RuntimeEventType;

typedef enum Ai2RuntimeHookAction {
    AI2_RUNTIME_HOOK_CONTINUE = 0,
    AI2_RUNTIME_HOOK_STOP = 1
} Ai2RuntimeHookAction;

typedef struct Ai2RuntimeEvent {
    Ai2RuntimeEventType type;
    uint64_t sequence;
    uint64_t instruction_index;
    uint64_t cpu_cycle;
    uint64_t ppu_cycle;
    uint64_t frame;
    uint8_t physical_prg_bank;
    uint8_t bus_value;
    uint8_t bus_is_write;
    uint8_t reserved;
    uint16_t program_counter;
    uint16_t bus_address;
    Ai2CoreTrapInfo trap;
} Ai2RuntimeEvent;

typedef Ai2RuntimeHookAction (*Ai2RuntimeHook)(const Ai2RuntimeEvent *event,
                                                void *user_data);

typedef struct Ai2CoreObservation {
    Ai2CoreStatus status;
    Ai2CoreTrapInfo trap;
    uint8_t physical_prg_bank;
    uint8_t player1_pending;
    uint8_t player2_pending;
    uint8_t stopped;
    uint16_t program_counter;
    uint16_t reserved;
    uint64_t executed_instructions;
    uint64_t cpu_cycles;
    uint64_t ppu_cycles;
    uint64_t apu_cycles;
    uint64_t frames;
    uint64_t controller_reads;
    uint64_t controller_latches;
    uint64_t pcm_total_samples;
    size_t pcm_available;
} Ai2CoreObservation;

typedef enum Ai2SnapshotStatus {
    AI2_SNAPSHOT_OK = 0,
    AI2_SNAPSHOT_INVALID_ARGUMENT,
    AI2_SNAPSHOT_NO_MEMORY,
    AI2_SNAPSHOT_IO_ERROR,
    AI2_SNAPSHOT_BAD_FORMAT,
    AI2_SNAPSHOT_UNSUPPORTED_VERSION,
    AI2_SNAPSHOT_ROM_MISMATCH,
    AI2_SNAPSHOT_CORE_MISMATCH,
    AI2_SNAPSHOT_PAYLOAD_HASH_MISMATCH,
    AI2_SNAPSHOT_TRAILING_DATA,
    AI2_SNAPSHOT_INVALID_STATE,
    AI2_SNAPSHOT_NOT_SAFE_BOUNDARY
} Ai2SnapshotStatus;

Ai2StaticCore *ai2_static_core_create(void);
void ai2_static_core_destroy(Ai2StaticCore *core);

/* The core copies the exact pinned ROM; the caller retains no ownership burden. */
int ai2_static_core_reset(Ai2StaticCore *core, const void *rom_bytes, size_t rom_size);
int ai2_static_core_advance_frame(Ai2StaticCore *core,
                                  uint8_t player1_buttons,
                                  uint8_t player2_buttons,
                                  uint64_t instruction_limit,
                                  Ai2FrameResult *result);
void ai2_static_core_set_controller(Ai2StaticCore *core, unsigned port,
                                    uint8_t buttons);

int ai2_static_core_frame_copy_indexed(const Ai2StaticCore *core,
                                       uint16_t *output,
                                       size_t pixel_capacity);
int ai2_static_core_frame_copy_bgra(const Ai2StaticCore *core,
                                    uint32_t *output,
                                    size_t pixel_capacity);
size_t ai2_static_core_audio_available(const Ai2StaticCore *core);
size_t ai2_static_core_audio_read(Ai2StaticCore *core,
                                  int16_t *output,
                                  size_t sample_capacity);
void ai2_static_core_audio_clear(Ai2StaticCore *core);

Ai2CoreStatus ai2_static_core_status(const Ai2StaticCore *core);
const char *ai2_static_core_status_name(Ai2CoreStatus status);
int ai2_static_core_trap(const Ai2StaticCore *core, Ai2CoreTrapInfo *trap);
int ai2_static_core_observe(const Ai2StaticCore *core, Ai2CoreObservation *observation);

void ai2_static_core_set_runtime_hook(Ai2StaticCore *core,
                                      uint32_t event_mask,
                                      Ai2RuntimeHook hook,
                                      void *user_data);
void ai2_static_core_clear_runtime_hook(Ai2StaticCore *core);
void ai2_static_core_request_stop(Ai2StaticCore *core);
int ai2_static_core_is_stopped(const Ai2StaticCore *core);
void ai2_static_core_resume(Ai2StaticCore *core);

Ai2SnapshotStatus ai2_static_core_snapshot_save(const Ai2StaticCore *core,
                                                const char *utf8_path,
                                                char *error,
                                                size_t error_capacity);
Ai2SnapshotStatus ai2_static_core_snapshot_load(Ai2StaticCore *core,
                                                const char *utf8_path,
                                                char *error,
                                                size_t error_capacity);
const char *ai2_snapshot_status_name(Ai2SnapshotStatus status);

const char *ai2_static_core_rom_sha256(void);
const char *ai2_static_core_compatibility_sha256(void);

#ifdef __cplusplus
}
#endif
#endif
