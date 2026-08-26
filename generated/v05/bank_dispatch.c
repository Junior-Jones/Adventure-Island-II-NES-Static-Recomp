/* Compact deterministic physical-bank/PC dispatcher. No opcode fetch or decode. */
#include "ai2_v05_generated.h"

int ai2_v05_generated_identity_exists(uint8_t bank, uint16_t pc)
{
    switch (bank) {
        case 0x00u: return ai2_v05_bank_00_has(pc);
        case 0x01u: return ai2_v05_bank_01_has(pc);
        case 0x02u: return ai2_v05_bank_02_has(pc);
        case 0x03u: return ai2_v05_bank_03_has(pc);
        case 0x04u: return ai2_v05_bank_04_has(pc);
        case 0x05u: return ai2_v05_bank_05_has(pc);
        case 0x06u: return ai2_v05_bank_06_has(pc);
        case 0x07u: return ai2_v05_bank_07_has(pc);
        case 0x08u: return ai2_v05_bank_08_has(pc);
        case 0x09u: return ai2_v05_bank_09_has(pc);
        case 0x0Au: return ai2_v05_bank_10_has(pc);
        case 0x0Bu: return ai2_v05_bank_11_has(pc);
        case 0x0Cu: return ai2_v05_bank_12_has(pc);
        case 0x0Du: return ai2_v05_bank_13_has(pc);
        case 0x0Eu: return ai2_v05_bank_14_has(pc);
        case 0x0Fu: return ai2_v05_bank_15_has(pc);
        default: return 0;
    }
}

Ai2V05StepResult ai2_v05_execute_compact(Ai2V05Runtime *rt, const Ai2V05CompactInstruction *rows, size_t count, uint8_t source_bank, uint16_t pc, Ai2V05Identity *next)
{
    size_t lo = 0u, hi = count;
    const Ai2V05CompactInstruction *row;
    Ai2V05SemanticResult sem = {0u, 0u};
    uint8_t dynamic_bank = 0u, origin_bank = 0u;
    uint16_t origin_pc = 0u;
    if (rt == NULL || rows == NULL || next == NULL) return AI2_V05_STEP_SEMANTIC_TRAP;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (pc == rows[mid].pc) { lo = mid; break; }
        if (pc < rows[mid].pc) hi = mid; else lo = mid + 1u;
    }
    if (lo >= count || rows[lo].pc != pc) return AI2_V05_STEP_MISSING_IDENTITY;
    row = &rows[lo];
    if ((row->flags & AI2_V05_COMPACT_NOTE_STACK_ORIGIN) != 0u && ai2_v05_runtime_note_stack_dispatch_origin(rt, source_bank, pc) != 0) return AI2_V05_STEP_SEMANTIC_TRAP;
    if (row->semantic == NULL || row->semantic(rt, pc, row->operand, &sem) != 0) return AI2_V05_STEP_SEMANTIC_TRAP;
    switch ((Ai2V05CompactFlow)row->flow) {
        case AI2_V05_FLOW_STATIC:
            next->bank = row->primary_bank; next->pc = row->primary_pc; return AI2_V05_STEP_OK;
        case AI2_V05_FLOW_BRANCH:
            if (sem.branch_taken != 0u) { next->bank = row->primary_bank; next->pc = row->primary_pc; }
            else { next->bank = row->secondary_bank; next->pc = row->secondary_pc; }
            return AI2_V05_STEP_OK;
        case AI2_V05_FLOW_INDIRECT:
            if (ai2_v05_runtime_map_prg_identity(rt, sem.dynamic_pc, &dynamic_bank) != 0 || !ai2_v05_validate_indirect(source_bank, pc, dynamic_bank, sem.dynamic_pc)) return AI2_V05_STEP_BAD_DYNAMIC_TARGET;
            break;
        case AI2_V05_FLOW_STACK_INDIRECT:
            if (ai2_v05_runtime_map_prg_identity(rt, sem.dynamic_pc, &dynamic_bank) != 0 || ai2_v05_runtime_get_stack_dispatch_origin(rt, &origin_bank, &origin_pc) != 0 || !ai2_v05_validate_stack_dispatch(origin_bank, origin_pc, dynamic_bank, sem.dynamic_pc)) return AI2_V05_STEP_BAD_DYNAMIC_TARGET;
            ai2_v05_runtime_clear_stack_dispatch_origin(rt);
            break;
        case AI2_V05_FLOW_RETURN:
            if (ai2_v05_runtime_map_prg_identity(rt, sem.dynamic_pc, &dynamic_bank) != 0 || !ai2_v05_validate_return(dynamic_bank, sem.dynamic_pc)) return AI2_V05_STEP_BAD_DYNAMIC_TARGET;
            break;
        case AI2_V05_FLOW_RTI:
            if (ai2_v05_runtime_map_prg_identity(rt, sem.dynamic_pc, &dynamic_bank) != 0 || !ai2_v05_generated_identity_exists(dynamic_bank, sem.dynamic_pc)) return AI2_V05_STEP_BAD_DYNAMIC_TARGET;
            break;
        default: return AI2_V05_STEP_SEMANTIC_TRAP;
    }
    next->bank = dynamic_bank; next->pc = sem.dynamic_pc; return AI2_V05_STEP_OK;
}

Ai2V05StepResult ai2_v05_dispatch(Ai2V05Runtime *rt, Ai2V05Identity current, Ai2V05Identity *next)
{
    switch (current.bank) {
        case 0x00u: return ai2_v05_bank_00_step(rt, current.pc, next);
        case 0x01u: return ai2_v05_bank_01_step(rt, current.pc, next);
        case 0x02u: return ai2_v05_bank_02_step(rt, current.pc, next);
        case 0x03u: return ai2_v05_bank_03_step(rt, current.pc, next);
        case 0x04u: return ai2_v05_bank_04_step(rt, current.pc, next);
        case 0x05u: return ai2_v05_bank_05_step(rt, current.pc, next);
        case 0x06u: return ai2_v05_bank_06_step(rt, current.pc, next);
        case 0x07u: return ai2_v05_bank_07_step(rt, current.pc, next);
        case 0x08u: return ai2_v05_bank_08_step(rt, current.pc, next);
        case 0x09u: return ai2_v05_bank_09_step(rt, current.pc, next);
        case 0x0Au: return ai2_v05_bank_10_step(rt, current.pc, next);
        case 0x0Bu: return ai2_v05_bank_11_step(rt, current.pc, next);
        case 0x0Cu: return ai2_v05_bank_12_step(rt, current.pc, next);
        case 0x0Du: return ai2_v05_bank_13_step(rt, current.pc, next);
        case 0x0Eu: return ai2_v05_bank_14_step(rt, current.pc, next);
        case 0x0Fu: return ai2_v05_bank_15_step(rt, current.pc, next);
        default: return AI2_V05_STEP_MISSING_BANK;
    }
}
