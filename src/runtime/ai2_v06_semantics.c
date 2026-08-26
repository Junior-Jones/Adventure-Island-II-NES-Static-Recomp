#include "ai2_v06_runtime.h"

#include <stddef.h>

static int sem_ok(const Ai2V05Runtime *rt)
{
    return rt != NULL && !ai2_v06_has_trap(rt) ? 0 : -1;
}

static int sem_begin(Ai2V05Runtime *rt, uint16_t source_pc, uint8_t opcode,
                     Ai2V05SemanticResult *result)
{
    if(rt == NULL || result == NULL || ai2_v06_has_trap(rt)) {
        return -1;
    }
    result->dynamic_pc = 0u;
    result->branch_taken = 0u;
    rt->p = (uint8_t)((rt->p | AI2_V06_FLAG_U) & (uint8_t)~AI2_V06_FLAG_B);
    ai2_v06_code_cycle(rt, source_pc, opcode, AI2_V06_BUS_CODE_CYCLE);
    return sem_ok(rt);
}

static void sem_operand_cycle(Ai2V05Runtime *rt, uint16_t source_pc, unsigned offset, uint8_t value)
{
    ai2_v06_code_cycle(rt, (uint16_t)(source_pc + offset), value, AI2_V06_BUS_OPERAND_CYCLE);
}

static int sem_read(Ai2V05Runtime *rt, uint16_t address, Ai2V06BusAccessKind kind, uint8_t *value)
{
    return ai2_v06_bus_read(rt, address, kind, value);
}

static int sem_write(Ai2V05Runtime *rt, uint16_t address, uint8_t value, Ai2V06BusAccessKind kind)
{
    return ai2_v06_bus_write(rt, address, value, kind);
}

static int sem_dummy_read(Ai2V05Runtime *rt, uint16_t address)
{
    uint8_t ignored = 0u;
    return sem_read(rt, address, AI2_V06_BUS_DUMMY_READ, &ignored);
}

static int sem_pc_dummy(Ai2V05Runtime *rt, uint16_t pc)
{
    return sem_dummy_read(rt, (uint16_t)(pc + 1u));
}

static int sem_push(Ai2V05Runtime *rt, uint8_t value)
{
    uint16_t address = (uint16_t)(0x0100u | rt->s);
    if(sem_write(rt, address, value, AI2_V06_BUS_STACK_WRITE) != 0) return -1;
    rt->s--;
    return 0;
}

static int sem_stack_dummy(Ai2V05Runtime *rt)
{
    return sem_dummy_read(rt, (uint16_t)(0x0100u | rt->s));
}

static int sem_pop(Ai2V05Runtime *rt, uint8_t *value)
{
    rt->s++;
    return sem_read(rt, (uint16_t)(0x0100u | rt->s), AI2_V06_BUS_STACK_READ, value);
}

static void sem_set_nz(Ai2V05Runtime *rt, uint8_t value)
{
    rt->p = (uint8_t)(rt->p & (uint8_t)~(AI2_V06_FLAG_N | AI2_V06_FLAG_Z));
    if(value == 0u) rt->p |= AI2_V06_FLAG_Z;
    if((value & 0x80u) != 0u) rt->p |= AI2_V06_FLAG_N;
    rt->p |= AI2_V06_FLAG_U;
    rt->p &= (uint8_t)~AI2_V06_FLAG_B;
}

static void sem_ora(Ai2V05Runtime *rt, uint8_t value) { rt->a = (uint8_t)(rt->a | value); sem_set_nz(rt, rt->a); }
static void sem_and(Ai2V05Runtime *rt, uint8_t value) { rt->a = (uint8_t)(rt->a & value); sem_set_nz(rt, rt->a); }
static void sem_eor(Ai2V05Runtime *rt, uint8_t value) { rt->a = (uint8_t)(rt->a ^ value); sem_set_nz(rt, rt->a); }
static void sem_lda(Ai2V05Runtime *rt, uint8_t value) { rt->a = value; sem_set_nz(rt, rt->a); }
static void sem_ldx(Ai2V05Runtime *rt, uint8_t value) { rt->x = value; sem_set_nz(rt, rt->x); }
static void sem_ldy(Ai2V05Runtime *rt, uint8_t value) { rt->y = value; sem_set_nz(rt, rt->y); }

static void sem_adc(Ai2V05Runtime *rt, uint8_t value)
{
    uint8_t old_a = rt->a;
    uint16_t sum = (uint16_t)old_a + (uint16_t)value + ((rt->p & AI2_V06_FLAG_C) != 0u ? 1u : 0u);
    uint8_t result = (uint8_t)sum;
    rt->p = (uint8_t)(rt->p & (uint8_t)~(AI2_V06_FLAG_C | AI2_V06_FLAG_V | AI2_V06_FLAG_N | AI2_V06_FLAG_Z));
    if(sum > 0xFFu) rt->p |= AI2_V06_FLAG_C;
    if(((uint8_t)~(old_a ^ value) & (uint8_t)(old_a ^ result) & 0x80u) != 0u) rt->p |= AI2_V06_FLAG_V;
    rt->a = result;
    sem_set_nz(rt, result);
}

static void sem_sbc(Ai2V05Runtime *rt, uint8_t value)
{
    sem_adc(rt, (uint8_t)(value ^ 0xFFu));
}

static void sem_cmp_value(Ai2V05Runtime *rt, uint8_t reg, uint8_t value)
{
    uint8_t result = (uint8_t)(reg - value);
    rt->p = (uint8_t)(rt->p & (uint8_t)~(AI2_V06_FLAG_C | AI2_V06_FLAG_N | AI2_V06_FLAG_Z));
    if(reg >= value) rt->p |= AI2_V06_FLAG_C;
    if(reg == value) rt->p |= AI2_V06_FLAG_Z;
    if((result & 0x80u) != 0u) rt->p |= AI2_V06_FLAG_N;
    rt->p |= AI2_V06_FLAG_U;
    rt->p &= (uint8_t)~AI2_V06_FLAG_B;
}

static void sem_cmp_a(Ai2V05Runtime *rt, uint8_t value) { sem_cmp_value(rt, rt->a, value); }
static void sem_cmp_x(Ai2V05Runtime *rt, uint8_t value) { sem_cmp_value(rt, rt->x, value); }
static void sem_cmp_y(Ai2V05Runtime *rt, uint8_t value) { sem_cmp_value(rt, rt->y, value); }

static void sem_bit(Ai2V05Runtime *rt, uint8_t value)
{
    rt->p = (uint8_t)(rt->p & (uint8_t)~(AI2_V06_FLAG_Z | AI2_V06_FLAG_V | AI2_V06_FLAG_N));
    if((rt->a & value) == 0u) rt->p |= AI2_V06_FLAG_Z;
    if((value & 0x40u) != 0u) rt->p |= AI2_V06_FLAG_V;
    if((value & 0x80u) != 0u) rt->p |= AI2_V06_FLAG_N;
    rt->p |= AI2_V06_FLAG_U;
    rt->p &= (uint8_t)~AI2_V06_FLAG_B;
}

static uint8_t sem_asl(Ai2V05Runtime *rt, uint8_t value)
{
    uint8_t result;
    rt->p = (uint8_t)(rt->p & (uint8_t)~AI2_V06_FLAG_C);
    if((value & 0x80u) != 0u) rt->p |= AI2_V06_FLAG_C;
    result = (uint8_t)(value << 1);
    sem_set_nz(rt, result);
    return result;
}

static uint8_t sem_lsr(Ai2V05Runtime *rt, uint8_t value)
{
    uint8_t result;
    rt->p = (uint8_t)(rt->p & (uint8_t)~AI2_V06_FLAG_C);
    if((value & 1u) != 0u) rt->p |= AI2_V06_FLAG_C;
    result = (uint8_t)(value >> 1);
    sem_set_nz(rt, result);
    return result;
}

static uint8_t sem_rol(Ai2V05Runtime *rt, uint8_t value)
{
    uint8_t old_c = (rt->p & AI2_V06_FLAG_C) != 0u ? 1u : 0u;
    uint8_t result;
    rt->p = (uint8_t)(rt->p & (uint8_t)~AI2_V06_FLAG_C);
    if((value & 0x80u) != 0u) rt->p |= AI2_V06_FLAG_C;
    result = (uint8_t)((value << 1) | old_c);
    sem_set_nz(rt, result);
    return result;
}

static uint8_t sem_ror(Ai2V05Runtime *rt, uint8_t value)
{
    uint8_t old_c = (rt->p & AI2_V06_FLAG_C) != 0u ? 0x80u : 0u;
    uint8_t result;
    rt->p = (uint8_t)(rt->p & (uint8_t)~AI2_V06_FLAG_C);
    if((value & 1u) != 0u) rt->p |= AI2_V06_FLAG_C;
    result = (uint8_t)((value >> 1) | old_c);
    sem_set_nz(rt, result);
    return result;
}

static uint8_t sem_inc(Ai2V05Runtime *rt, uint8_t value) { value++; sem_set_nz(rt, value); return value; }
static uint8_t sem_dec(Ai2V05Runtime *rt, uint8_t value) { value--; sem_set_nz(rt, value); return value; }

static uint16_t sem_addr_zero(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand)
{
    sem_operand_cycle(rt, pc, 1u, (uint8_t)operand);
    return (uint8_t)operand;
}

static uint16_t sem_addr_zerox(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand)
{
    uint8_t base = (uint8_t)operand;
    sem_operand_cycle(rt, pc, 1u, base);
    if(sem_dummy_read(rt, base) != 0) return 0u;
    return (uint8_t)(base + rt->x);
}

static uint16_t sem_addr_abs(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand)
{
    sem_operand_cycle(rt, pc, 1u, (uint8_t)operand);
    sem_operand_cycle(rt, pc, 2u, (uint8_t)(operand >> 8));
    return operand;
}

static uint16_t sem_addr_absx(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, int always_dummy)
{
    uint16_t eff;
    int crossed;
    sem_addr_abs(rt, pc, operand);
    eff = (uint16_t)(operand + rt->x);
    crossed = (operand & 0xFF00u) != (eff & 0xFF00u);
    if(always_dummy || crossed) {
        uint16_t dummy = (uint16_t)((operand & 0xFF00u) | (eff & 0x00FFu));
        if(sem_dummy_read(rt, dummy) != 0) return 0u;
    }
    return eff;
}

static uint16_t sem_addr_absy(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, int always_dummy)
{
    uint16_t eff;
    int crossed;
    sem_addr_abs(rt, pc, operand);
    eff = (uint16_t)(operand + rt->y);
    crossed = (operand & 0xFF00u) != (eff & 0xFF00u);
    if(always_dummy || crossed) {
        uint16_t dummy = (uint16_t)((operand & 0xFF00u) | (eff & 0x00FFu));
        if(sem_dummy_read(rt, dummy) != 0) return 0u;
    }
    return eff;
}

static uint16_t sem_addr_indy(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, int always_dummy)
{
    uint8_t zp = (uint8_t)operand;
    uint8_t lo = 0u, hi = 0u;
    uint16_t base, eff;
    int crossed;
    sem_operand_cycle(rt, pc, 1u, zp);
    if(sem_read(rt, zp, AI2_V06_BUS_READ, &lo) != 0) return 0u;
    if(sem_read(rt, (uint8_t)(zp + 1u), AI2_V06_BUS_READ, &hi) != 0) return 0u;
    base = (uint16_t)(lo | ((uint16_t)hi << 8));
    eff = (uint16_t)(base + rt->y);
    crossed = (base & 0xFF00u) != (eff & 0xFF00u);
    if(always_dummy || crossed) {
        uint16_t dummy = (uint16_t)((base & 0xFF00u) | (eff & 0x00FFu));
        if(sem_dummy_read(rt, dummy) != 0) return 0u;
    }
    return eff;
}

#define DEF_IMM_READ(SYM, OPC, ACTION) \
int SYM(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *result) { \
    if(sem_begin(rt, pc, (OPC), result) != 0) return -1; \
    sem_operand_cycle(rt, pc, 1u, (uint8_t)operand); \
    ACTION(rt, (uint8_t)operand); \
    return sem_ok(rt); \
}

#define DEF_ZERO_READ(SYM, OPC, ACTION) \
int SYM(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *result) { \
    uint8_t v = 0u; uint16_t a; \
    if(sem_begin(rt, pc, (OPC), result) != 0) return -1; \
    a = sem_addr_zero(rt, pc, operand); if(sem_read(rt, a, AI2_V06_BUS_READ, &v) != 0) return -1; \
    ACTION(rt, v); return sem_ok(rt); \
}

#define DEF_ZEROX_READ(SYM, OPC, ACTION) \
int SYM(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *result) { \
    uint8_t v = 0u; uint16_t a; \
    if(sem_begin(rt, pc, (OPC), result) != 0) return -1; \
    a = sem_addr_zerox(rt, pc, operand); if(sem_ok(rt) != 0 || sem_read(rt, a, AI2_V06_BUS_READ, &v) != 0) return -1; \
    ACTION(rt, v); return sem_ok(rt); \
}

#define DEF_ABS_READ(SYM, OPC, ACTION) \
int SYM(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *result) { \
    uint8_t v = 0u; uint16_t a; \
    if(sem_begin(rt, pc, (OPC), result) != 0) return -1; \
    a = sem_addr_abs(rt, pc, operand); if(sem_read(rt, a, AI2_V06_BUS_READ, &v) != 0) return -1; \
    ACTION(rt, v); return sem_ok(rt); \
}

#define DEF_ABSX_READ(SYM, OPC, ACTION) \
int SYM(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *result) { \
    uint8_t v = 0u; uint16_t a; \
    if(sem_begin(rt, pc, (OPC), result) != 0) return -1; \
    a = sem_addr_absx(rt, pc, operand, 0); if(sem_ok(rt) != 0 || sem_read(rt, a, AI2_V06_BUS_READ, &v) != 0) return -1; \
    ACTION(rt, v); return sem_ok(rt); \
}

#define DEF_ABSY_READ(SYM, OPC, ACTION) \
int SYM(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *result) { \
    uint8_t v = 0u; uint16_t a; \
    if(sem_begin(rt, pc, (OPC), result) != 0) return -1; \
    a = sem_addr_absy(rt, pc, operand, 0); if(sem_ok(rt) != 0 || sem_read(rt, a, AI2_V06_BUS_READ, &v) != 0) return -1; \
    ACTION(rt, v); return sem_ok(rt); \
}

#define DEF_INDY_READ(SYM, OPC, ACTION) \
int SYM(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *result) { \
    uint8_t v = 0u; uint16_t a; \
    if(sem_begin(rt, pc, (OPC), result) != 0) return -1; \
    a = sem_addr_indy(rt, pc, operand, 0); if(sem_ok(rt) != 0 || sem_read(rt, a, AI2_V06_BUS_READ, &v) != 0) return -1; \
    ACTION(rt, v); return sem_ok(rt); \
}

#define DEF_ZERO_STORE(SYM, OPC, REG) \
int SYM(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *result) { \
    uint16_t a; if(sem_begin(rt, pc, (OPC), result) != 0) return -1; \
    a = sem_addr_zero(rt, pc, operand); return sem_write(rt, a, rt->REG, AI2_V06_BUS_WRITE); \
}

#define DEF_ZEROX_STORE(SYM, OPC, REG) \
int SYM(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *result) { \
    uint16_t a; if(sem_begin(rt, pc, (OPC), result) != 0) return -1; \
    a = sem_addr_zerox(rt, pc, operand); if(sem_ok(rt) != 0) return -1; return sem_write(rt, a, rt->REG, AI2_V06_BUS_WRITE); \
}

#define DEF_ABS_STORE(SYM, OPC, REG) \
int SYM(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *result) { \
    uint16_t a; if(sem_begin(rt, pc, (OPC), result) != 0) return -1; \
    a = sem_addr_abs(rt, pc, operand); return sem_write(rt, a, rt->REG, AI2_V06_BUS_WRITE); \
}

#define DEF_ABSX_STORE(SYM, OPC, REG) \
int SYM(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *result) { \
    uint16_t a; if(sem_begin(rt, pc, (OPC), result) != 0) return -1; \
    a = sem_addr_absx(rt, pc, operand, 1); if(sem_ok(rt) != 0) return -1; return sem_write(rt, a, rt->REG, AI2_V06_BUS_WRITE); \
}

#define DEF_ABSY_STORE(SYM, OPC, REG) \
int SYM(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *result) { \
    uint16_t a; if(sem_begin(rt, pc, (OPC), result) != 0) return -1; \
    a = sem_addr_absy(rt, pc, operand, 1); if(sem_ok(rt) != 0) return -1; return sem_write(rt, a, rt->REG, AI2_V06_BUS_WRITE); \
}

#define DEF_INDY_STORE(SYM, OPC, REG) \
int SYM(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *result) { \
    uint16_t a; if(sem_begin(rt, pc, (OPC), result) != 0) return -1; \
    a = sem_addr_indy(rt, pc, operand, 1); if(sem_ok(rt) != 0) return -1; return sem_write(rt, a, rt->REG, AI2_V06_BUS_WRITE); \
}

#define DEF_RMW_ZERO(SYM, OPC, TRANSFORM) \
int SYM(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *result) { \
    uint16_t a; uint8_t oldv = 0u, newv; if(sem_begin(rt, pc, (OPC), result) != 0) return -1; \
    a = sem_addr_zero(rt, pc, operand); if(sem_read(rt, a, AI2_V06_BUS_READ, &oldv) != 0) return -1; \
    if(sem_write(rt, a, oldv, AI2_V06_BUS_RMW_WRITE_OLD) != 0) { return -1; } \
    newv = TRANSFORM(rt, oldv); \
    return sem_write(rt, a, newv, AI2_V06_BUS_RMW_WRITE_NEW); \
}

#define DEF_RMW_ZEROX(SYM, OPC, TRANSFORM) \
int SYM(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *result) { \
    uint16_t a; uint8_t oldv = 0u, newv; if(sem_begin(rt, pc, (OPC), result) != 0) return -1; \
    a = sem_addr_zerox(rt, pc, operand); if(sem_ok(rt) != 0 || sem_read(rt, a, AI2_V06_BUS_READ, &oldv) != 0) return -1; \
    if(sem_write(rt, a, oldv, AI2_V06_BUS_RMW_WRITE_OLD) != 0) { return -1; } \
    newv = TRANSFORM(rt, oldv); \
    return sem_write(rt, a, newv, AI2_V06_BUS_RMW_WRITE_NEW); \
}

#define DEF_RMW_ABS(SYM, OPC, TRANSFORM) \
int SYM(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *result) { \
    uint16_t a; uint8_t oldv = 0u, newv; if(sem_begin(rt, pc, (OPC), result) != 0) return -1; \
    a = sem_addr_abs(rt, pc, operand); if(sem_read(rt, a, AI2_V06_BUS_READ, &oldv) != 0) return -1; \
    if(sem_write(rt, a, oldv, AI2_V06_BUS_RMW_WRITE_OLD) != 0) { return -1; } \
    newv = TRANSFORM(rt, oldv); \
    return sem_write(rt, a, newv, AI2_V06_BUS_RMW_WRITE_NEW); \
}

#define DEF_RMW_ABSX(SYM, OPC, TRANSFORM) \
int SYM(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *result) { \
    uint16_t a; uint8_t oldv = 0u, newv; if(sem_begin(rt, pc, (OPC), result) != 0) return -1; \
    a = sem_addr_absx(rt, pc, operand, 1); if(sem_ok(rt) != 0 || sem_read(rt, a, AI2_V06_BUS_READ, &oldv) != 0) return -1; \
    if(sem_write(rt, a, oldv, AI2_V06_BUS_RMW_WRITE_OLD) != 0) { return -1; } \
    newv = TRANSFORM(rt, oldv); \
    return sem_write(rt, a, newv, AI2_V06_BUS_RMW_WRITE_NEW); \
}

#define DEF_BRANCH(SYM, OPC, CONDITION) \
int SYM(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *result) { \
    int8_t rel; uint16_t base, target; if(sem_begin(rt, pc, (OPC), result) != 0) return -1; \
    sem_operand_cycle(rt, pc, 1u, (uint8_t)operand); rel = (int8_t)(uint8_t)operand; base = (uint16_t)(pc + 2u); \
    if(CONDITION) { result->branch_taken = 1u; if(sem_dummy_read(rt, base) != 0) return -1; target = (uint16_t)(base + rel); \
        if((base & 0xFF00u) != (target & 0xFF00u)) { uint16_t d = (uint16_t)((base & 0xFF00u) | (target & 0x00FFu)); if(sem_dummy_read(rt, d) != 0) return -1; } } \
    return sem_ok(rt); \
}

/* Fixed read/ALU forms. */
DEF_ZERO_READ(ai2_v05_op_05_ORA_Zero, 0x05u, sem_ora)
DEF_IMM_READ(ai2_v05_op_09_ORA_Imm, 0x09u, sem_ora)
DEF_ABSX_READ(ai2_v05_op_1D_ORA_AbsX, 0x1Du, sem_ora)
DEF_ZERO_READ(ai2_v05_op_24_BIT_Zero, 0x24u, sem_bit)
DEF_ZERO_READ(ai2_v05_op_25_AND_Zero, 0x25u, sem_and)
DEF_IMM_READ(ai2_v05_op_29_AND_Imm, 0x29u, sem_and)
DEF_ZEROX_READ(ai2_v05_op_35_AND_ZeroX, 0x35u, sem_and)
DEF_IMM_READ(ai2_v05_op_49_EOR_Imm, 0x49u, sem_eor)
DEF_ZEROX_READ(ai2_v05_op_55_EOR_ZeroX, 0x55u, sem_eor)
DEF_ZERO_READ(ai2_v05_op_65_ADC_Zero, 0x65u, sem_adc)
DEF_IMM_READ(ai2_v05_op_69_ADC_Imm, 0x69u, sem_adc)
DEF_ABS_READ(ai2_v05_op_6D_ADC_Abs, 0x6Du, sem_adc)
DEF_ABSY_READ(ai2_v05_op_79_ADC_AbsY, 0x79u, sem_adc)
DEF_ABSX_READ(ai2_v05_op_7D_ADC_AbsX, 0x7Du, sem_adc)
DEF_IMM_READ(ai2_v05_op_A0_LDY_Imm, 0xA0u, sem_ldy)
DEF_IMM_READ(ai2_v05_op_A2_LDX_Imm, 0xA2u, sem_ldx)
DEF_ZERO_READ(ai2_v05_op_A4_LDY_Zero, 0xA4u, sem_ldy)
DEF_ZERO_READ(ai2_v05_op_A5_LDA_Zero, 0xA5u, sem_lda)
DEF_ZERO_READ(ai2_v05_op_A6_LDX_Zero, 0xA6u, sem_ldx)
DEF_IMM_READ(ai2_v05_op_A9_LDA_Imm, 0xA9u, sem_lda)
DEF_ABS_READ(ai2_v05_op_AC_LDY_Abs, 0xACu, sem_ldy)
DEF_ABS_READ(ai2_v05_op_AD_LDA_Abs, 0xADu, sem_lda)
DEF_ABS_READ(ai2_v05_op_AE_LDX_Abs, 0xAEu, sem_ldx)
DEF_INDY_READ(ai2_v05_op_B1_LDA_IndY, 0xB1u, sem_lda)
DEF_ZEROX_READ(ai2_v05_op_B5_LDA_ZeroX, 0xB5u, sem_lda)
DEF_ABSY_READ(ai2_v05_op_B9_LDA_AbsY, 0xB9u, sem_lda)
DEF_ABSX_READ(ai2_v05_op_BC_LDY_AbsX, 0xBCu, sem_ldy)
DEF_ABSX_READ(ai2_v05_op_BD_LDA_AbsX, 0xBDu, sem_lda)
DEF_IMM_READ(ai2_v05_op_C0_CPY_Imm, 0xC0u, sem_cmp_y)
DEF_ZERO_READ(ai2_v05_op_C4_CPY_Zero, 0xC4u, sem_cmp_y)
DEF_ZERO_READ(ai2_v05_op_C5_CMP_Zero, 0xC5u, sem_cmp_a)
DEF_IMM_READ(ai2_v05_op_C9_CMP_Imm, 0xC9u, sem_cmp_a)
DEF_INDY_READ(ai2_v05_op_D1_CMP_IndY, 0xD1u, sem_cmp_a)
DEF_ABSX_READ(ai2_v05_op_DD_CMP_AbsX, 0xDDu, sem_cmp_a)
DEF_IMM_READ(ai2_v05_op_E0_CPX_Imm, 0xE0u, sem_cmp_x)
DEF_ZERO_READ(ai2_v05_op_E4_CPX_Zero, 0xE4u, sem_cmp_x)
DEF_ZERO_READ(ai2_v05_op_E5_SBC_Zero, 0xE5u, sem_sbc)
DEF_IMM_READ(ai2_v05_op_E9_SBC_Imm, 0xE9u, sem_sbc)
DEF_ABSY_READ(ai2_v05_op_F9_SBC_AbsY, 0xF9u, sem_sbc)
DEF_ABSX_READ(ai2_v05_op_FD_SBC_AbsX, 0xFDu, sem_sbc)

/* Fixed stores. */
DEF_ZERO_STORE(ai2_v05_op_84_STY_Zero, 0x84u, y)
DEF_ZERO_STORE(ai2_v05_op_85_STA_Zero, 0x85u, a)
DEF_ZERO_STORE(ai2_v05_op_86_STX_Zero, 0x86u, x)
DEF_ABS_STORE(ai2_v05_op_8C_STY_Abs, 0x8Cu, y)
DEF_ABS_STORE(ai2_v05_op_8D_STA_Abs, 0x8Du, a)
DEF_ABS_STORE(ai2_v05_op_8E_STX_Abs, 0x8Eu, x)
DEF_INDY_STORE(ai2_v05_op_91_STA_IndYW, 0x91u, a)
DEF_ZEROX_STORE(ai2_v05_op_94_STY_ZeroX, 0x94u, y)
DEF_ZEROX_STORE(ai2_v05_op_95_STA_ZeroX, 0x95u, a)
DEF_ABSY_STORE(ai2_v05_op_99_STA_AbsYW, 0x99u, a)
DEF_ABSX_STORE(ai2_v05_op_9D_STA_AbsXW, 0x9Du, a)

/* Read-modify-write forms. */
DEF_RMW_ZERO(ai2_v05_op_06_ASL_Zero, 0x06u, sem_asl)
DEF_RMW_ZERO(ai2_v05_op_26_ROL_Zero, 0x26u, sem_rol)
DEF_RMW_ZERO(ai2_v05_op_46_LSR_Zero, 0x46u, sem_lsr)
DEF_RMW_ZERO(ai2_v05_op_66_ROR_Zero, 0x66u, sem_ror)
DEF_RMW_ZERO(ai2_v05_op_C6_DEC_Zero, 0xC6u, sem_dec)
DEF_RMW_ABS(ai2_v05_op_CE_DEC_Abs, 0xCEu, sem_dec)
DEF_RMW_ZEROX(ai2_v05_op_D6_DEC_ZeroX, 0xD6u, sem_dec)
DEF_RMW_ABSX(ai2_v05_op_DE_DEC_AbsXW, 0xDEu, sem_dec)
DEF_RMW_ZERO(ai2_v05_op_E6_INC_Zero, 0xE6u, sem_inc)
DEF_RMW_ABS(ai2_v05_op_EE_INC_Abs, 0xEEu, sem_inc)
DEF_RMW_ZEROX(ai2_v05_op_F6_INC_ZeroX, 0xF6u, sem_inc)
DEF_RMW_ABSX(ai2_v05_op_FE_INC_AbsXW, 0xFEu, sem_inc)

/* Branches. */
DEF_BRANCH(ai2_v05_op_10_BPL_Rel, 0x10u, (rt->p & AI2_V06_FLAG_N) == 0u)
DEF_BRANCH(ai2_v05_op_30_BMI_Rel, 0x30u, (rt->p & AI2_V06_FLAG_N) != 0u)
DEF_BRANCH(ai2_v05_op_50_BVC_Rel, 0x50u, (rt->p & AI2_V06_FLAG_V) == 0u)
DEF_BRANCH(ai2_v05_op_70_BVS_Rel, 0x70u, (rt->p & AI2_V06_FLAG_V) != 0u)
DEF_BRANCH(ai2_v05_op_90_BCC_Rel, 0x90u, (rt->p & AI2_V06_FLAG_C) == 0u)
DEF_BRANCH(ai2_v05_op_B0_BCS_Rel, 0xB0u, (rt->p & AI2_V06_FLAG_C) != 0u)
DEF_BRANCH(ai2_v05_op_D0_BNE_Rel, 0xD0u, (rt->p & AI2_V06_FLAG_Z) == 0u)
DEF_BRANCH(ai2_v05_op_F0_BEQ_Rel, 0xF0u, (rt->p & AI2_V06_FLAG_Z) != 0u)

/* Accumulator shifts and simple implied operations. */
int ai2_v05_op_0A_ASL_Acc(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *r) { (void)operand; if(sem_begin(rt,pc,0x0Au,r)!=0)return -1; if(sem_pc_dummy(rt,pc)!=0)return -1; rt->a=sem_asl(rt,rt->a); return sem_ok(rt); }
int ai2_v05_op_2A_ROL_Acc(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *r) { (void)operand; if(sem_begin(rt,pc,0x2Au,r)!=0)return -1; if(sem_pc_dummy(rt,pc)!=0)return -1; rt->a=sem_rol(rt,rt->a); return sem_ok(rt); }
int ai2_v05_op_4A_LSR_Acc(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *r) { (void)operand; if(sem_begin(rt,pc,0x4Au,r)!=0)return -1; if(sem_pc_dummy(rt,pc)!=0)return -1; rt->a=sem_lsr(rt,rt->a); return sem_ok(rt); }
int ai2_v05_op_6A_ROR_Acc(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *r) { (void)operand; if(sem_begin(rt,pc,0x6Au,r)!=0)return -1; if(sem_pc_dummy(rt,pc)!=0)return -1; rt->a=sem_ror(rt,rt->a); return sem_ok(rt); }

#define DEF_IMPLIED(SYM, OPC, BODY) \
int SYM(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *r) { \
    (void)operand; if(sem_begin(rt,pc,(OPC),r)!=0)return -1; if(sem_pc_dummy(rt,pc)!=0)return -1; BODY; return sem_ok(rt); \
}

DEF_IMPLIED(ai2_v05_op_18_CLC_Imp, 0x18u, rt->p=(uint8_t)((rt->p & (uint8_t)~AI2_V06_FLAG_C)|AI2_V06_FLAG_U))
DEF_IMPLIED(ai2_v05_op_38_SEC_Imp, 0x38u, rt->p=(uint8_t)(rt->p|AI2_V06_FLAG_C|AI2_V06_FLAG_U))
DEF_IMPLIED(ai2_v05_op_78_SEI_Imp, 0x78u, rt->p=(uint8_t)(rt->p|AI2_V06_FLAG_I|AI2_V06_FLAG_U))
DEF_IMPLIED(ai2_v05_op_88_DEY_Imp, 0x88u, rt->y--; sem_set_nz(rt,rt->y))
DEF_IMPLIED(ai2_v05_op_8A_TXA_Imp, 0x8Au, rt->a=rt->x; sem_set_nz(rt,rt->a))
DEF_IMPLIED(ai2_v05_op_98_TYA_Imp, 0x98u, rt->a=rt->y; sem_set_nz(rt,rt->a))
DEF_IMPLIED(ai2_v05_op_9A_TXS_Imp, 0x9Au, rt->s=rt->x)
DEF_IMPLIED(ai2_v05_op_A8_TAY_Imp, 0xA8u, rt->y=rt->a; sem_set_nz(rt,rt->y))
DEF_IMPLIED(ai2_v05_op_AA_TAX_Imp, 0xAAu, rt->x=rt->a; sem_set_nz(rt,rt->x))
DEF_IMPLIED(ai2_v05_op_BA_TSX_Imp, 0xBAu, rt->x=rt->s; sem_set_nz(rt,rt->x))
DEF_IMPLIED(ai2_v05_op_C8_INY_Imp, 0xC8u, rt->y++; sem_set_nz(rt,rt->y))
DEF_IMPLIED(ai2_v05_op_CA_DEX_Imp, 0xCAu, rt->x--; sem_set_nz(rt,rt->x))
DEF_IMPLIED(ai2_v05_op_D8_CLD_Imp, 0xD8u, rt->p=(uint8_t)((rt->p & (uint8_t)~AI2_V06_FLAG_D)|AI2_V06_FLAG_U))
DEF_IMPLIED(ai2_v05_op_E8_INX_Imp, 0xE8u, rt->x++; sem_set_nz(rt,rt->x))
DEF_IMPLIED(ai2_v05_op_EA_NOP_Imp, 0xEAu, (void)0)

/* Stack operations. */
int ai2_v05_op_08_PHP_Imp(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *r)
{
    (void)operand; if(sem_begin(rt,pc,0x08u,r)!=0)return -1; if(sem_pc_dummy(rt,pc)!=0)return -1;
    return sem_push(rt, (uint8_t)(rt->p | AI2_V06_FLAG_B | AI2_V06_FLAG_U));
}
int ai2_v05_op_48_PHA_Imp(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *r)
{
    (void)operand; if(sem_begin(rt,pc,0x48u,r)!=0)return -1; if(sem_pc_dummy(rt,pc)!=0)return -1; return sem_push(rt, rt->a);
}
int ai2_v05_op_28_PLP_Imp(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *r)
{
    uint8_t v = 0u;
    (void)operand;
    if(sem_begin(rt, pc, 0x28u, r) != 0) return -1;
    if(sem_pc_dummy(rt, pc) != 0) return -1;
    if(sem_stack_dummy(rt) != 0 || sem_pop(rt, &v) != 0) return -1;
    rt->p = (uint8_t)((v & (uint8_t)~AI2_V06_FLAG_B) | AI2_V06_FLAG_U);
    return 0;
}
int ai2_v05_op_68_PLA_Imp(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *r)
{
    uint8_t v = 0u;
    (void)operand;
    if(sem_begin(rt, pc, 0x68u, r) != 0) return -1;
    if(sem_pc_dummy(rt, pc) != 0) return -1;
    if(sem_stack_dummy(rt) != 0 || sem_pop(rt, &v) != 0) return -1;
    rt->a = v;
    sem_set_nz(rt, v);
    return 0;
}

/* Calls, returns and jumps. */
int ai2_v05_op_20_JSR_Abs(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *r)
{
    uint16_t ret=(uint16_t)(pc+2u); if(sem_begin(rt,pc,0x20u,r)!=0)return -1;
    sem_operand_cycle(rt,pc,1u,(uint8_t)operand); if(sem_stack_dummy(rt)!=0)return -1;
    if(sem_push(rt,(uint8_t)(ret>>8))!=0 || sem_push(rt,(uint8_t)ret)!=0)return -1;
    sem_operand_cycle(rt,pc,2u,(uint8_t)(operand>>8)); return sem_ok(rt);
}
int ai2_v05_op_4C_JMP_Abs(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *r)
{
    if(sem_begin(rt, pc, 0x4Cu, r) != 0) return -1;
    (void)sem_addr_abs(rt, pc, operand);
    return sem_ok(rt);
}
int ai2_v05_op_6C_JMP_Ind(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *r)
{
    uint8_t lo = 0u;
    uint8_t hi = 0u;
    uint16_t high_addr;
    if(sem_begin(rt, pc, 0x6Cu, r) != 0) return -1;
    (void)sem_addr_abs(rt, pc, operand);
    if(sem_read(rt, operand, AI2_V06_BUS_READ, &lo) != 0) return -1;
    high_addr = (uint16_t)((operand & 0xFF00u) | ((operand + 1u) & 0x00FFu));
    if(sem_read(rt, high_addr, AI2_V06_BUS_READ, &hi) != 0) return -1;
    r->dynamic_pc = (uint16_t)(lo | ((uint16_t)hi << 8));
    return 0;
}
int ai2_v05_op_60_RTS_Imp(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *r)
{
    uint8_t lo = 0u;
    uint8_t hi = 0u;
    uint16_t ret;
    (void)operand;
    if(sem_begin(rt, pc, 0x60u, r) != 0) return -1;
    if(sem_pc_dummy(rt, pc) != 0) return -1;
    if(sem_stack_dummy(rt) != 0 || sem_pop(rt, &lo) != 0 || sem_pop(rt, &hi) != 0) return -1;
    ret = (uint16_t)(lo | ((uint16_t)hi << 8));
    if(sem_dummy_read(rt, ret) != 0) return -1;
    r->dynamic_pc = (uint16_t)(ret + 1u);
    return 0;
}
int ai2_v05_op_40_RTI_Imp(Ai2V05Runtime *rt, uint16_t pc, uint16_t operand, Ai2V05SemanticResult *r)
{
    uint8_t p=0u,lo=0u,hi=0u; (void)operand; if(sem_begin(rt,pc,0x40u,r)!=0)return -1; if(sem_pc_dummy(rt,pc)!=0)return -1;
    if(sem_stack_dummy(rt)!=0 || sem_pop(rt,&p)!=0 || sem_pop(rt,&lo)!=0 || sem_pop(rt,&hi)!=0)return -1;
    rt->p=(uint8_t)((p & (uint8_t)~AI2_V06_FLAG_B)|AI2_V06_FLAG_U); r->dynamic_pc=(uint16_t)(lo|((uint16_t)hi<<8)); return 0;
}

