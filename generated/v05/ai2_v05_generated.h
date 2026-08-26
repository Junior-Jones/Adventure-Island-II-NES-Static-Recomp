#ifndef AI2_V05_GENERATED_H
#define AI2_V05_GENERATED_H
#include <stddef.h>
#include <stdint.h>
#include "ai2_v05_runtime_api.h"

typedef struct Ai2V05Identity { uint8_t bank; uint16_t pc; } Ai2V05Identity;
typedef int (*Ai2V05SemanticFn)(Ai2V05Runtime *, uint16_t, uint16_t, Ai2V05SemanticResult *);
typedef enum Ai2V05CompactFlow {
    AI2_V05_FLOW_STATIC = 0,
    AI2_V05_FLOW_BRANCH = 1,
    AI2_V05_FLOW_INDIRECT = 2,
    AI2_V05_FLOW_STACK_INDIRECT = 3,
    AI2_V05_FLOW_RETURN = 4,
    AI2_V05_FLOW_RTI = 5
} Ai2V05CompactFlow;
typedef enum Ai2V05CompactFlags {
    AI2_V05_COMPACT_NONE = 0,
    AI2_V05_COMPACT_NOTE_STACK_ORIGIN = 1
} Ai2V05CompactFlags;
typedef struct Ai2V05CompactInstruction {
    Ai2V05SemanticFn semantic;
    uint16_t pc, operand, primary_pc, secondary_pc;
    uint8_t primary_bank, secondary_bank, flow, flags;
} Ai2V05CompactInstruction;
typedef enum Ai2V05StepResult {
    AI2_V05_STEP_OK = 0,
    AI2_V05_STEP_MISSING_IDENTITY = 1,
    AI2_V05_STEP_MISSING_BANK = 2,
    AI2_V05_STEP_SEMANTIC_TRAP = 3,
    AI2_V05_STEP_BAD_DYNAMIC_TARGET = 4
} Ai2V05StepResult;

Ai2V05StepResult ai2_v05_dispatch(Ai2V05Runtime *rt, Ai2V05Identity current, Ai2V05Identity *next);
int ai2_v05_generated_identity_exists(uint8_t bank, uint16_t pc);
int ai2_v05_validate_indirect(uint8_t source_bank, uint16_t source_pc, uint8_t target_bank, uint16_t target_pc);
int ai2_v05_validate_stack_dispatch(uint8_t origin_bank, uint16_t origin_pc, uint8_t target_bank, uint16_t target_pc);
int ai2_v05_validate_return(uint8_t target_bank, uint16_t target_pc);
Ai2V05StepResult ai2_v05_execute_compact(Ai2V05Runtime *rt, const Ai2V05CompactInstruction *rows, size_t count, uint8_t source_bank, uint16_t pc, Ai2V05Identity *next);

extern const uint16_t ai2_v05_bank_00_identity_pcs[];
extern const size_t ai2_v05_bank_00_identity_count;
int ai2_v05_bank_00_has(uint16_t pc);
Ai2V05StepResult ai2_v05_bank_00_step(Ai2V05Runtime *rt, uint16_t pc, Ai2V05Identity *next);
extern const uint16_t ai2_v05_bank_01_identity_pcs[];
extern const size_t ai2_v05_bank_01_identity_count;
int ai2_v05_bank_01_has(uint16_t pc);
Ai2V05StepResult ai2_v05_bank_01_step(Ai2V05Runtime *rt, uint16_t pc, Ai2V05Identity *next);
extern const uint16_t ai2_v05_bank_02_identity_pcs[];
extern const size_t ai2_v05_bank_02_identity_count;
int ai2_v05_bank_02_has(uint16_t pc);
Ai2V05StepResult ai2_v05_bank_02_step(Ai2V05Runtime *rt, uint16_t pc, Ai2V05Identity *next);
extern const uint16_t ai2_v05_bank_03_identity_pcs[];
extern const size_t ai2_v05_bank_03_identity_count;
int ai2_v05_bank_03_has(uint16_t pc);
Ai2V05StepResult ai2_v05_bank_03_step(Ai2V05Runtime *rt, uint16_t pc, Ai2V05Identity *next);
extern const uint16_t ai2_v05_bank_04_identity_pcs[];
extern const size_t ai2_v05_bank_04_identity_count;
int ai2_v05_bank_04_has(uint16_t pc);
Ai2V05StepResult ai2_v05_bank_04_step(Ai2V05Runtime *rt, uint16_t pc, Ai2V05Identity *next);
extern const uint16_t ai2_v05_bank_05_identity_pcs[];
extern const size_t ai2_v05_bank_05_identity_count;
int ai2_v05_bank_05_has(uint16_t pc);
Ai2V05StepResult ai2_v05_bank_05_step(Ai2V05Runtime *rt, uint16_t pc, Ai2V05Identity *next);
extern const uint16_t ai2_v05_bank_06_identity_pcs[];
extern const size_t ai2_v05_bank_06_identity_count;
int ai2_v05_bank_06_has(uint16_t pc);
Ai2V05StepResult ai2_v05_bank_06_step(Ai2V05Runtime *rt, uint16_t pc, Ai2V05Identity *next);
extern const uint16_t ai2_v05_bank_07_identity_pcs[];
extern const size_t ai2_v05_bank_07_identity_count;
int ai2_v05_bank_07_has(uint16_t pc);
Ai2V05StepResult ai2_v05_bank_07_step(Ai2V05Runtime *rt, uint16_t pc, Ai2V05Identity *next);
extern const uint16_t ai2_v05_bank_08_identity_pcs[];
extern const size_t ai2_v05_bank_08_identity_count;
int ai2_v05_bank_08_has(uint16_t pc);
Ai2V05StepResult ai2_v05_bank_08_step(Ai2V05Runtime *rt, uint16_t pc, Ai2V05Identity *next);
extern const uint16_t ai2_v05_bank_09_identity_pcs[];
extern const size_t ai2_v05_bank_09_identity_count;
int ai2_v05_bank_09_has(uint16_t pc);
Ai2V05StepResult ai2_v05_bank_09_step(Ai2V05Runtime *rt, uint16_t pc, Ai2V05Identity *next);
extern const uint16_t ai2_v05_bank_10_identity_pcs[];
extern const size_t ai2_v05_bank_10_identity_count;
int ai2_v05_bank_10_has(uint16_t pc);
Ai2V05StepResult ai2_v05_bank_10_step(Ai2V05Runtime *rt, uint16_t pc, Ai2V05Identity *next);
extern const uint16_t ai2_v05_bank_11_identity_pcs[];
extern const size_t ai2_v05_bank_11_identity_count;
int ai2_v05_bank_11_has(uint16_t pc);
Ai2V05StepResult ai2_v05_bank_11_step(Ai2V05Runtime *rt, uint16_t pc, Ai2V05Identity *next);
extern const uint16_t ai2_v05_bank_12_identity_pcs[];
extern const size_t ai2_v05_bank_12_identity_count;
int ai2_v05_bank_12_has(uint16_t pc);
Ai2V05StepResult ai2_v05_bank_12_step(Ai2V05Runtime *rt, uint16_t pc, Ai2V05Identity *next);
extern const uint16_t ai2_v05_bank_13_identity_pcs[];
extern const size_t ai2_v05_bank_13_identity_count;
int ai2_v05_bank_13_has(uint16_t pc);
Ai2V05StepResult ai2_v05_bank_13_step(Ai2V05Runtime *rt, uint16_t pc, Ai2V05Identity *next);
extern const uint16_t ai2_v05_bank_14_identity_pcs[];
extern const size_t ai2_v05_bank_14_identity_count;
int ai2_v05_bank_14_has(uint16_t pc);
Ai2V05StepResult ai2_v05_bank_14_step(Ai2V05Runtime *rt, uint16_t pc, Ai2V05Identity *next);
extern const uint16_t ai2_v05_bank_15_identity_pcs[];
extern const size_t ai2_v05_bank_15_identity_count;
int ai2_v05_bank_15_has(uint16_t pc);
Ai2V05StepResult ai2_v05_bank_15_step(Ai2V05Runtime *rt, uint16_t pc, Ai2V05Identity *next);

#endif
