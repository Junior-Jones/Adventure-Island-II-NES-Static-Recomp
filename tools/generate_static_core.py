#!/usr/bin/env python3
"""Adventure Island II V05 deterministic fixed-translation generator.

Input authority is the sealed V04 static instruction/control-flow proof plus the
exact external ROM used only to verify that the proof still matches immutable PRG
bytes.  The generated C never fetches or decodes a game opcode at runtime.
"""
from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
import shutil
from pathlib import Path

TOOL_NAME = "ai2-static-generator"
TOOL_VERSION = "0.5.1-v05-compact"
EXPECTED_ROM_SHA256 = "10dd53fd62c95e77856b09c693b3bdd14b62a89988902eea111257cafe9b8acc"
EXPECTED_PAYLOAD_SHA256 = "5ee479ef04d80366a75dd144cbeb47d0347ee82025d1b65dd358e1f6498c9a39"
EXPECTED_PRG_SHA256 = "e021cedf0b7700b3eee7e5b76b2e23000075e06f4d7c13a4b556452952d3aec0"
BRANCHES = {"BPL", "BMI", "BVC", "BVS", "BCC", "BCS", "BNE", "BEQ"}
CONTROL = BRANCHES | {"JMP", "JSR", "RTS", "RTI", "BRK"}


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def c_hex16(value: int) -> str:
    return f"0x{value:04X}u"


def c_hex8(value: int) -> str:
    return f"0x{value:02X}u"


def parse_int(value: str) -> int:
    return int(value, 0)


def operand_value(byte_hex: str) -> int:
    raw = bytes.fromhex(byte_hex)
    if len(raw) <= 1:
        return 0
    if len(raw) == 2:
        return raw[1]
    return raw[1] | (raw[2] << 8)


def semantic_symbol(opcode: int, mnemonic: str, mode: str) -> str:
    safe_mode = "".join(ch if ch.isalnum() else "_" for ch in mode)
    return f"ai2_v05_op_{opcode:02X}_{mnemonic}_{safe_mode}"


def bus_contract(mnemonic: str, mode: str) -> list[str]:
    """V05 ordering contract. V06 implements the exact cycles behind each token."""
    rmw = mnemonic in {"ASL", "LSR", "ROL", "ROR", "INC", "DEC"} and mode != "Acc"
    store = mnemonic in {"STA", "STX", "STY"}
    stack = {
        "PHA": ["stack-write"], "PHP": ["stack-write"],
        "PLA": ["stack-dummy-read", "stack-read"], "PLP": ["stack-dummy-read", "stack-read"],
        "JSR": ["operand-low", "stack-dummy-read", "stack-write-pch", "stack-write-pcl", "operand-high"],
        "RTS": ["dummy-read", "stack-dummy-read", "stack-read-pcl", "stack-read-pch", "return-increment"],
        "RTI": ["dummy-read", "stack-dummy-read", "stack-read-p", "stack-read-pcl", "stack-read-pch"],
    }
    if mnemonic in stack:
        return stack[mnemonic]
    if mnemonic == "JMP" and mode == "Ind":
        return ["pointer-low-read", "pointer-high-read-with-6502-page-wrap"]
    if mnemonic in BRANCHES:
        return ["test-flag", "taken-dummy-read", "page-cross-fixup-read-if-needed"]
    if mnemonic == "BRK":
        return ["padding-fetch", "stack-write-pch", "stack-write-pcl", "stack-write-p", "vector-low-read", "vector-high-read"]
    if rmw:
        return ["effective-address", "data-read", "write-old", "write-new"]
    if store:
        if mode in {"AbsXW", "AbsYW", "IndYW"}:
            return ["effective-address", "indexed-dummy-read", "data-write"]
        return ["effective-address", "data-write"]
    if mode == "Imm":
        return ["literal-operand", "alu-register-update"]
    if mode in {"Imp", "Acc"}:
        return ["internal-register-operation"]
    if mode in {"AbsX", "AbsY", "IndY"}:
        return ["effective-address", "data-read", "page-cross-fixup-read-if-needed", "alu-register-update"]
    return ["effective-address", "data-read", "alu-register-update"]


def load_and_validate(root: Path, rom_path: Path):
    raw = rom_path.read_bytes()
    if sha256_bytes(raw) != EXPECTED_ROM_SHA256:
        raise SystemExit("V05 exact-ROM SHA-256 mismatch")
    if len(raw) < 16 or raw[:4] != b"NES\x1a":
        raise SystemExit("V05 invalid iNES image")
    payload = raw[16:]
    if len(payload) != 262144 or sha256_bytes(payload) != EXPECTED_PAYLOAD_SHA256:
        raise SystemExit("V05 payload mismatch")
    prg = payload[:131072]
    if sha256_bytes(prg) != EXPECTED_PRG_SHA256:
        raise SystemExit("V05 PRG mismatch")

    paths = {
        "instructions": root / "generated/v04/instructions.csv",
        "edges": root / "generated/v04/edges.csv",
        "indirect": root / "generated/v04/indirect-targets.csv",
        "returns": root / "generated/v04/return-continuations.csv",
        "unresolved": root / "generated/v04/unresolved-edges.csv",
        "closure": root / "generated/v04/closure-summary.json",
        "opcodes": root / "config/2a03-opcodes.json",
        "proofs": root / "config/v04-static-proofs.json",
    }
    for key, path in paths.items():
        if not path.is_file():
            raise SystemExit(f"V05 missing required {key}: {path}")

    closure = json.loads(paths["closure"].read_text(encoding="utf-8"))
    expected = {
        "accepted_instruction_contexts": 16544,
        "accepted_instruction_bytes": 35423,
        "return_continuations": 1133,
        "proven_indirect_targets": 404,
        "unresolved_current_graph_edges": 0,
        "decode_or_inline_overlaps": 0,
    }
    for key, value in expected.items():
        if closure.get(key) != value:
            raise SystemExit(f"V05 refuses changed V04 closure field {key}: {closure.get(key)!r}")
    if closure.get("rom_wide_closure_claimed") is not False:
        raise SystemExit("V05 expects truthful V04 current-graph-only closure statement")

    unresolved = read_csv(paths["unresolved"])
    if unresolved:
        raise SystemExit(f"V05 refuses {len(unresolved)} unresolved V04 current-graph edge(s)")

    instructions = read_csv(paths["instructions"])
    edges = read_csv(paths["edges"])
    indirect = read_csv(paths["indirect"])
    returns = read_csv(paths["returns"])
    opcodes = json.loads(paths["opcodes"].read_text(encoding="utf-8"))["entries"]
    optab = {int(item["opcode"]): item for item in opcodes}

    if len(instructions) != 16544 or len(indirect) != 404 or len(returns) != 1133:
        raise SystemExit("V05 input authority row counts changed")

    seen: set[tuple[int, int]] = set()
    instruction_map: dict[tuple[int, int], dict[str, object]] = {}
    for row in instructions:
        bank = int(row["physical_bank"])
        pc = parse_int(row["cpu_pc"])
        key = (bank, pc)
        if key in seen:
            raise SystemExit(f"V05 duplicate instruction identity {bank}:{pc:04X}")
        seen.add(key)
        raw_bytes = bytes.fromhex(row["bytes"])
        off = bank * 0x2000 + (pc & 0x1FFF)
        if prg[off:off + len(raw_bytes)] != raw_bytes:
            raise SystemExit(f"V05 V04 instruction bytes disagree with ROM at {bank}:{pc:04X}")
        opcode = raw_bytes[0]
        meta = optab[opcode]
        if meta["mnemonic"] != row["mnemonic"] or meta["mode"] != row["mode"] or meta["size"] != int(row["size"]):
            raise SystemExit(f"V05 opcode metadata mismatch at {bank}:{pc:04X}")
        instruction_map[key] = {
            "bank": bank, "pc": pc, "size": int(row["size"]),
            "mnemonic": row["mnemonic"], "mode": row["mode"], "bytes": row["bytes"],
            "opcode": opcode, "operand": operand_value(row["bytes"]),
        }

    edge_map: dict[tuple[int, int], list[dict[str, object]]] = collections.defaultdict(list)
    for row in edges:
        source = (int(row["source_bank"]), parse_int(row["source_pc"]))
        if source not in instruction_map:
            raise SystemExit(f"V05 edge source is not an accepted identity: {source}")
        edge_map[source].append({
            "kind": row["kind"],
            "target_bank": None if not row["target_bank"] else int(row["target_bank"]),
            "target_pc": None if not row["target_pc"] else parse_int(row["target_pc"]),
            "reason": row["reason"],
        })

    return raw, prg, paths, closure, instruction_map, edge_map, indirect, returns, optab


def get_edge(edges: list[dict[str, object]], kind: str) -> dict[str, object]:
    matches = [edge for edge in edges if edge["kind"] == kind]
    if len(matches) != 1:
        raise SystemExit(f"V05 expected exactly one {kind} edge, got {len(matches)}")
    return matches[0]


def emit_bank(bank: int, rows: list[dict[str, object]], edge_map, out: Path) -> None:
    pcs = [int(row["pc"]) for row in rows]
    lines = [
        f"/* Generated by {TOOL_NAME} {TOOL_VERSION}. Do not edit. */",
        '#include "ai2_v05_generated.h"',
        "",
        f"const uint16_t ai2_v05_bank_{bank:02d}_identity_pcs[] = {{",
    ]
    if pcs:
        for i in range(0, len(pcs), 12):
            lines.append("    " + ", ".join(c_hex16(pc) for pc in pcs[i:i+12]) + ",")
    else:
        lines.append("    0u, /* sentinel; count is zero */")
    lines.extend([
        "};",
        f"const size_t ai2_v05_bank_{bank:02d}_identity_count = {len(pcs)}u;",
        "",
        f"int ai2_v05_bank_{bank:02d}_has(uint16_t pc)",
        "{",
        "    size_t lo = 0u;",
        f"    size_t hi = ai2_v05_bank_{bank:02d}_identity_count;",
        "    while (lo < hi) {",
        "        size_t mid = lo + (hi - lo) / 2u;",
        f"        uint16_t value = ai2_v05_bank_{bank:02d}_identity_pcs[mid];",
        "        if (pc == value) return 1;",
        "        if (pc < value) hi = mid; else lo = mid + 1u;",
        "    }",
        "    return 0;",
        "}",
        "",
        f"static const Ai2V05CompactInstruction k_bank_{bank:02d}_instructions[] = {{",
    ])

    for row in rows:
        pc = int(row["pc"]); opcode = int(row["opcode"]); mnemonic = str(row["mnemonic"]); mode = str(row["mode"])
        operand = int(row["operand"]); edges = edge_map[(bank, pc)]
        symbol = semantic_symbol(opcode, mnemonic, mode)
        primary_bank = 0
        primary_pc = 0
        secondary_bank = 0
        secondary_pc = 0
        flow = "AI2_V05_FLOW_STATIC"
        flags = "AI2_V05_COMPACT_NONE"
        if mnemonic in BRANCHES:
            taken = get_edge(edges, "branch"); fall = get_edge(edges, "fallthrough")
            primary_bank, primary_pc = int(taken["target_bank"]), int(taken["target_pc"])
            secondary_bank, secondary_pc = int(fall["target_bank"]), int(fall["target_pc"])
            flow = "AI2_V05_FLOW_BRANCH"
        elif mnemonic == "JMP" and mode == "Abs":
            jump = get_edge(edges, "jump")
            primary_bank, primary_pc = int(jump["target_bank"]), int(jump["target_pc"])
        elif mnemonic == "JMP" and mode == "Ind":
            if bank == 15 and pc == 0xEDB6:
                flow = "AI2_V05_FLOW_STACK_INDIRECT"
            else:
                flow = "AI2_V05_FLOW_INDIRECT"
        elif mnemonic == "JSR":
            kind = "call-stack-dispatcher" if any(e["kind"] == "call-stack-dispatcher" for e in edges) else "call-return-adjuster" if any(e["kind"] == "call-return-adjuster" for e in edges) else "call"
            call = get_edge(edges, kind)
            primary_bank, primary_pc = int(call["target_bank"]), int(call["target_pc"])
            if kind == "call-stack-dispatcher":
                flags = "AI2_V05_COMPACT_NOTE_STACK_ORIGIN"
        elif mnemonic == "RTS":
            flow = "AI2_V05_FLOW_RETURN"
        elif mnemonic == "RTI":
            flow = "AI2_V05_FLOW_RTI"
        elif mnemonic == "BRK":
            brk = get_edge(edges, "brk-vector")
            primary_bank, primary_pc = int(brk["target_bank"]), int(brk["target_pc"])
        else:
            fall = get_edge(edges, "fallthrough")
            primary_bank, primary_pc = int(fall["target_bank"]), int(fall["target_pc"])

        lines.append(
            f"    {{{symbol}, {c_hex16(pc)}, {c_hex16(operand)}, "
            f"{c_hex16(primary_pc)}, {c_hex16(secondary_pc)}, {c_hex8(primary_bank)}, "
            f"{c_hex8(secondary_bank)}, {flow}, {flags}}}, "
            f"/* {bank}:{pc:04X} {mnemonic} {mode}; fixed opcode form {opcode:02X} */"
        )

    if not rows:
        lines.append("    {NULL, 0u, 0u, 0u, 0u, 0u, 0u, AI2_V05_FLOW_STATIC, AI2_V05_COMPACT_NONE},")
    lines.extend([
        "};",
        "",
        f"Ai2V05StepResult ai2_v05_bank_{bank:02d}_step(Ai2V05Runtime *rt, uint16_t pc, Ai2V05Identity *next)",
        "{",
        f"    return ai2_v05_execute_compact(rt, k_bank_{bank:02d}_instructions, ai2_v05_bank_{bank:02d}_identity_count, {c_hex8(bank)}, pc, next);",
        "}",
        "",
    ])
    write_text(out / f"bank_{bank:02d}.c", "\n".join(lines))


def emit_headers(out: Path, forms: list[tuple[int, str, str]], per_bank: dict[int, int]) -> None:
    runtime = [
        "#ifndef AI2_V05_RUNTIME_API_H",
        "#define AI2_V05_RUNTIME_API_H",
        "#include <stdint.h>",
        "",
        "typedef struct Ai2V05Runtime Ai2V05Runtime;",
        "typedef struct Ai2V05SemanticResult { uint16_t dynamic_pc; uint8_t branch_taken; } Ai2V05SemanticResult;",
        "",
        "/* V06 must implement these mapper/runtime helpers. They never fetch an opcode. */",
        "int ai2_v05_runtime_map_prg_identity(Ai2V05Runtime *rt, uint16_t pc, uint8_t *physical_bank);",
        "int ai2_v05_runtime_note_stack_dispatch_origin(Ai2V05Runtime *rt, uint8_t bank, uint16_t pc);",
        "int ai2_v05_runtime_get_stack_dispatch_origin(Ai2V05Runtime *rt, uint8_t *bank, uint16_t *pc);",
        "void ai2_v05_runtime_clear_stack_dispatch_origin(Ai2V05Runtime *rt);",
        "",
        "/* Each symbol is one fixed 2A03 opcode form selected at generation time. */",
    ]
    for opcode, mnemonic, mode in forms:
        runtime.append(f"int {semantic_symbol(opcode, mnemonic, mode)}(Ai2V05Runtime *rt, uint16_t source_pc, uint16_t operand, Ai2V05SemanticResult *result);")
    runtime.extend(["", "#endif", ""])
    write_text(out / "ai2_v05_runtime_api.h", "\n".join(runtime))

    header = [
        "#ifndef AI2_V05_GENERATED_H",
        "#define AI2_V05_GENERATED_H",
        "#include <stddef.h>",
        "#include <stdint.h>",
        '#include "ai2_v05_runtime_api.h"',
        "",
        "typedef struct Ai2V05Identity { uint8_t bank; uint16_t pc; } Ai2V05Identity;",
        "typedef int (*Ai2V05SemanticFn)(Ai2V05Runtime *, uint16_t, uint16_t, Ai2V05SemanticResult *);",
        "typedef enum Ai2V05CompactFlow {",
        "    AI2_V05_FLOW_STATIC = 0,",
        "    AI2_V05_FLOW_BRANCH = 1,",
        "    AI2_V05_FLOW_INDIRECT = 2,",
        "    AI2_V05_FLOW_STACK_INDIRECT = 3,",
        "    AI2_V05_FLOW_RETURN = 4,",
        "    AI2_V05_FLOW_RTI = 5",
        "} Ai2V05CompactFlow;",
        "typedef enum Ai2V05CompactFlags {",
        "    AI2_V05_COMPACT_NONE = 0,",
        "    AI2_V05_COMPACT_NOTE_STACK_ORIGIN = 1",
        "} Ai2V05CompactFlags;",
        "typedef struct Ai2V05CompactInstruction {",
        "    Ai2V05SemanticFn semantic;",
        "    uint16_t pc, operand, primary_pc, secondary_pc;",
        "    uint8_t primary_bank, secondary_bank, flow, flags;",
        "} Ai2V05CompactInstruction;",
        "typedef enum Ai2V05StepResult {",
        "    AI2_V05_STEP_OK = 0,",
        "    AI2_V05_STEP_MISSING_IDENTITY = 1,",
        "    AI2_V05_STEP_MISSING_BANK = 2,",
        "    AI2_V05_STEP_SEMANTIC_TRAP = 3,",
        "    AI2_V05_STEP_BAD_DYNAMIC_TARGET = 4",
        "} Ai2V05StepResult;",
        "",
        "Ai2V05StepResult ai2_v05_dispatch(Ai2V05Runtime *rt, Ai2V05Identity current, Ai2V05Identity *next);",
        "int ai2_v05_generated_identity_exists(uint8_t bank, uint16_t pc);",
        "int ai2_v05_validate_indirect(uint8_t source_bank, uint16_t source_pc, uint8_t target_bank, uint16_t target_pc);",
        "int ai2_v05_validate_stack_dispatch(uint8_t origin_bank, uint16_t origin_pc, uint8_t target_bank, uint16_t target_pc);",
        "int ai2_v05_validate_return(uint8_t target_bank, uint16_t target_pc);",
        "Ai2V05StepResult ai2_v05_execute_compact(Ai2V05Runtime *rt, const Ai2V05CompactInstruction *rows, size_t count, uint8_t source_bank, uint16_t pc, Ai2V05Identity *next);",
        "",
    ]
    for bank in range(16):
        header.extend([
            f"extern const uint16_t ai2_v05_bank_{bank:02d}_identity_pcs[];",
            f"extern const size_t ai2_v05_bank_{bank:02d}_identity_count;",
            f"int ai2_v05_bank_{bank:02d}_has(uint16_t pc);",
            f"Ai2V05StepResult ai2_v05_bank_{bank:02d}_step(Ai2V05Runtime *rt, uint16_t pc, Ai2V05Identity *next);",
        ])
    header.extend(["", "#endif", ""])
    write_text(out / "ai2_v05_generated.h", "\n".join(header))


def emit_dispatch(out: Path) -> None:
    lines = [
        "/* Compact deterministic physical-bank/PC dispatcher. No opcode fetch or decode. */",
        '#include "ai2_v05_generated.h"',
        "",
        "int ai2_v05_generated_identity_exists(uint8_t bank, uint16_t pc)",
        "{",
        "    switch (bank) {",
    ]
    for bank in range(16):
        lines.append(f"        case {c_hex8(bank)}: return ai2_v05_bank_{bank:02d}_has(pc);")
    lines.extend(["        default: return 0;", "    }", "}", "",
                  "Ai2V05StepResult ai2_v05_execute_compact(Ai2V05Runtime *rt, const Ai2V05CompactInstruction *rows, size_t count, uint8_t source_bank, uint16_t pc, Ai2V05Identity *next)",
                  "{",
                  "    size_t lo = 0u, hi = count;",
                  "    const Ai2V05CompactInstruction *row;",
                  "    Ai2V05SemanticResult sem = {0u, 0u};",
                  "    uint8_t dynamic_bank = 0u, origin_bank = 0u;",
                  "    uint16_t origin_pc = 0u;",
                  "    if (rt == NULL || rows == NULL || next == NULL) return AI2_V05_STEP_SEMANTIC_TRAP;",
                  "    while (lo < hi) {",
                  "        size_t mid = lo + (hi - lo) / 2u;",
                  "        if (pc == rows[mid].pc) { lo = mid; break; }",
                  "        if (pc < rows[mid].pc) hi = mid; else lo = mid + 1u;",
                  "    }",
                  "    if (lo >= count || rows[lo].pc != pc) return AI2_V05_STEP_MISSING_IDENTITY;",
                  "    row = &rows[lo];",
                  "    if ((row->flags & AI2_V05_COMPACT_NOTE_STACK_ORIGIN) != 0u && ai2_v05_runtime_note_stack_dispatch_origin(rt, source_bank, pc) != 0) return AI2_V05_STEP_SEMANTIC_TRAP;",
                  "    if (row->semantic == NULL || row->semantic(rt, pc, row->operand, &sem) != 0) return AI2_V05_STEP_SEMANTIC_TRAP;",
                  "    switch ((Ai2V05CompactFlow)row->flow) {",
                  "        case AI2_V05_FLOW_STATIC:",
                  "            next->bank = row->primary_bank; next->pc = row->primary_pc; return AI2_V05_STEP_OK;",
                  "        case AI2_V05_FLOW_BRANCH:",
                  "            if (sem.branch_taken != 0u) { next->bank = row->primary_bank; next->pc = row->primary_pc; }",
                  "            else { next->bank = row->secondary_bank; next->pc = row->secondary_pc; }",
                  "            return AI2_V05_STEP_OK;",
                  "        case AI2_V05_FLOW_INDIRECT:",
                  "            if (ai2_v05_runtime_map_prg_identity(rt, sem.dynamic_pc, &dynamic_bank) != 0 || !ai2_v05_validate_indirect(source_bank, pc, dynamic_bank, sem.dynamic_pc)) return AI2_V05_STEP_BAD_DYNAMIC_TARGET;",
                  "            break;",
                  "        case AI2_V05_FLOW_STACK_INDIRECT:",
                  "            if (ai2_v05_runtime_map_prg_identity(rt, sem.dynamic_pc, &dynamic_bank) != 0 || ai2_v05_runtime_get_stack_dispatch_origin(rt, &origin_bank, &origin_pc) != 0 || !ai2_v05_validate_stack_dispatch(origin_bank, origin_pc, dynamic_bank, sem.dynamic_pc)) return AI2_V05_STEP_BAD_DYNAMIC_TARGET;",
                  "            ai2_v05_runtime_clear_stack_dispatch_origin(rt);",
                  "            break;",
                  "        case AI2_V05_FLOW_RETURN:",
                  "            if (ai2_v05_runtime_map_prg_identity(rt, sem.dynamic_pc, &dynamic_bank) != 0 || !ai2_v05_validate_return(dynamic_bank, sem.dynamic_pc)) return AI2_V05_STEP_BAD_DYNAMIC_TARGET;",
                  "            break;",
                  "        case AI2_V05_FLOW_RTI:",
                  "            if (ai2_v05_runtime_map_prg_identity(rt, sem.dynamic_pc, &dynamic_bank) != 0 || !ai2_v05_generated_identity_exists(dynamic_bank, sem.dynamic_pc)) return AI2_V05_STEP_BAD_DYNAMIC_TARGET;",
                  "            break;",
                  "        default: return AI2_V05_STEP_SEMANTIC_TRAP;",
                  "    }",
                  "    next->bank = dynamic_bank; next->pc = sem.dynamic_pc; return AI2_V05_STEP_OK;",
                  "}", "",
                  "Ai2V05StepResult ai2_v05_dispatch(Ai2V05Runtime *rt, Ai2V05Identity current, Ai2V05Identity *next)", "{", "    switch (current.bank) {"])
    for bank in range(16):
        lines.append(f"        case {c_hex8(bank)}: return ai2_v05_bank_{bank:02d}_step(rt, current.pc, next);")
    lines.extend(["        default: return AI2_V05_STEP_MISSING_BANK;", "    }", "}", ""])
    write_text(out / "bank_dispatch.c", "\n".join(lines))


def emit_domains(out: Path, indirect: list[dict[str, str]], returns: list[dict[str, str]]) -> None:
    word_rows = [row for row in indirect if row["kind"] == "word-table"]
    stack_rows = [row for row in indirect if row["kind"] == "inline-stack-table"]
    if len(word_rows) != 285 or len(stack_rows) != 119:
        raise SystemExit("V05 indirect-domain category counts changed")

    lines = [
        "/* Generated V04-proven finite dynamic-target domains. */",
        '#include "ai2_v05_generated.h"',
        "",
        "typedef struct Ai2V05IndirectRow { uint8_t source_bank; uint16_t source_pc; uint16_t entry; uint8_t target_bank; uint16_t target_pc; } Ai2V05IndirectRow;",
        "static const Ai2V05IndirectRow k_indirect_rows[] = {",
    ]
    for row in word_rows:
        lines.append(f"    {{{c_hex8(int(row['source_bank']))}, {c_hex16(parse_int(row['source_pc']))}, {int(row['entry'])}u, {c_hex8(int(row['target_bank']))}, {c_hex16(parse_int(row['target_pc']))}}},")
    lines.extend(["};", "", "int ai2_v05_validate_indirect(uint8_t source_bank, uint16_t source_pc, uint8_t target_bank, uint16_t target_pc)", "{",
                  "    size_t i;", "    for (i = 0u; i < sizeof(k_indirect_rows) / sizeof(k_indirect_rows[0]); ++i) {",
                  "        const Ai2V05IndirectRow *row = &k_indirect_rows[i];",
                  "        if (row->source_bank == source_bank && row->source_pc == source_pc && row->target_bank == target_bank && row->target_pc == target_pc) return 1;",
                  "    }", "    return 0;", "}", "",
                  "typedef struct Ai2V05StackRow { uint8_t origin_bank; uint16_t origin_pc; uint16_t entry; uint8_t target_bank; uint16_t target_pc; } Ai2V05StackRow;",
                  "static const Ai2V05StackRow k_stack_rows[] = {"])
    for row in stack_rows:
        lines.append(f"    {{{c_hex8(int(row['source_bank']))}, {c_hex16(parse_int(row['source_pc']))}, {int(row['entry'])}u, {c_hex8(int(row['target_bank']))}, {c_hex16(parse_int(row['target_pc']))}}},")
    lines.extend(["};", "", "int ai2_v05_validate_stack_dispatch(uint8_t origin_bank, uint16_t origin_pc, uint8_t target_bank, uint16_t target_pc)", "{",
                  "    size_t i;", "    for (i = 0u; i < sizeof(k_stack_rows) / sizeof(k_stack_rows[0]); ++i) {",
                  "        const Ai2V05StackRow *row = &k_stack_rows[i];",
                  "        if (row->origin_bank == origin_bank && row->origin_pc == origin_pc && row->target_bank == target_bank && row->target_pc == target_pc) return 1;",
                  "    }", "    return 0;", "}", ""])
    write_text(out / "indirect_domains.c", "\n".join(lines))

    unique_returns = sorted({(int(row["source_bank"]), parse_int(row["continuation_pc"])) for row in returns})
    rlines = [
        "/* Union of V04-proven JSR/return-adjusted continuation identities. */",
        '#include "ai2_v05_generated.h"',
        "",
        "typedef struct Ai2V05ReturnIdentity { uint8_t bank; uint16_t pc; } Ai2V05ReturnIdentity;",
        "static const Ai2V05ReturnIdentity k_return_identities[] = {",
    ]
    for bank, pc in unique_returns:
        rlines.append(f"    {{{c_hex8(bank)}, {c_hex16(pc)}}},")
    rlines.extend(["};", "", "int ai2_v05_validate_return(uint8_t target_bank, uint16_t target_pc)", "{",
                   "    size_t i;", "    for (i = 0u; i < sizeof(k_return_identities) / sizeof(k_return_identities[0]); ++i) {",
                   "        if (k_return_identities[i].bank == target_bank && k_return_identities[i].pc == target_pc) return 1;",
                   "    }", "    return 0;", "}", ""])
    write_text(out / "return_domains.c", "\n".join(rlines))


def ordered_manifest(out: Path, excluded: set[str] | None = None) -> dict[str, object]:
    excluded = excluded or set()
    files = []
    for path in sorted((p for p in out.rglob("*") if p.is_file()), key=lambda p: p.relative_to(out).as_posix()):
        rel = path.relative_to(out).as_posix()
        if rel in excluded:
            continue
        data = path.read_bytes()
        files.append({"path": rel, "bytes": len(data), "sha256": sha256_bytes(data)})
    aggregate = hashlib.sha256()
    for item in files:
        aggregate.update(str(item["path"]).encode("utf-8")); aggregate.update(b"\0")
        aggregate.update(str(item["sha256"]).encode("ascii")); aggregate.update(b"\n")
    return {"schema": 1, "file_count": len(files), "total_bytes": sum(int(item["bytes"]) for item in files), "aggregate_sha256": aggregate.hexdigest(), "files": files}


def generate(root: Path, rom: Path, out: Path) -> dict[str, object]:
    raw, prg, paths, closure, instruction_map, edge_map, indirect, returns, optab = load_and_validate(root, rom)
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True, exist_ok=True)

    by_bank: dict[int, list[dict[str, object]]] = {bank: [] for bank in range(16)}
    for row in instruction_map.values():
        by_bank[int(row["bank"])].append(row)
    for rows in by_bank.values():
        rows.sort(key=lambda row: int(row["pc"]))

    forms = sorted({(int(row["opcode"]), str(row["mnemonic"]), str(row["mode"])) for row in instruction_map.values()})
    emit_headers(out, forms, {bank: len(rows) for bank, rows in by_bank.items()})
    for bank in range(16):
        emit_bank(bank, by_bank[bank], edge_map, out)
    emit_dispatch(out)
    emit_domains(out, indirect, returns)

    contracts = []
    for opcode, mnemonic, mode in forms:
        contracts.append({
            "opcode_form": f"{opcode:02X}", "mnemonic": mnemonic, "mode": mode,
            "symbol": semantic_symbol(opcode, mnemonic, mode),
            "bus_sequence_contract": bus_contract(mnemonic, mode),
            "runtime_opcode_fetch": False,
            "runtime_opcode_decode": False,
        })
    write_text(out / "opcode-form-contracts.json", json.dumps({"schema": 1, "generator": f"{TOOL_NAME} {TOOL_VERSION}", "form_count": len(contracts), "forms": contracts}, indent=2, sort_keys=True) + "\n")

    sources = ["bank_dispatch.c", "indirect_domains.c", "return_domains.c"] + [f"bank_{bank:02d}.c" for bank in range(16)]
    cmake = ["# Generated explicit V05 source list; no globbing.", "set(AI2_V05_GENERATED_SOURCES"] + [f"    ${{CMAKE_CURRENT_LIST_DIR}}/{name}" for name in sources] + [")", ""]
    write_text(out / "generated_sources.cmake", "\n".join(cmake))

    per_bank = {str(bank): len(by_bank[bank]) for bank in range(16)}
    unique_returns = sorted({(int(row["source_bank"]), parse_int(row["continuation_pc"])) for row in returns})
    identity = {
        "schema": 1, "milestone": "V05", "generator": {"name": TOOL_NAME, "version": TOOL_VERSION},
        "accepted_instruction_contexts": len(instruction_map), "per_physical_prg_bank": per_bank,
        "opcode_form_count": len(forms), "proven_indirect_entries": len(indirect),
        "return_continuation_rows": len(returns), "unique_return_identities": len(unique_returns),
        "rom_wide_closure_claimed": False,
        "policy": "fixed physical-bank/PC dispatch; fixed opcode-form calls; no runtime opcode fetch/decode/interpreter/fallback",
    }
    write_text(out / "generated-identity.json", json.dumps(identity, indent=2, sort_keys=True) + "\n")

    content_manifest = ordered_manifest(out)
    source_hashes = {str(path.relative_to(root).as_posix()): sha256_file(path) for path in paths.values()}
    source_hashes["tools/generate_static_core.py"] = sha256_file(root / "tools/generate_static_core.py")
    receipt = {
        "schema": 1, "milestone": "V05", "tool": {"name": TOOL_NAME, "version": TOOL_VERSION},
        "normalized_arguments": ["--rom", "<EXTERNAL_ROM>", "--output", "<OUTPUT>", "--root", "<PROJECT_ROOT>"],
        "rom": {"file_sha256": EXPECTED_ROM_SHA256, "payload_sha256": EXPECTED_PAYLOAD_SHA256, "prg_sha256": EXPECTED_PRG_SHA256, "embedded_in_output": False},
        "authority": {"v04_instruction_contexts": len(instruction_map), "v04_indirect_entries": len(indirect), "v04_return_rows": len(returns), "rom_wide_closure_claimed": False},
        "generated": {"bank_files": 16, "instruction_contexts": len(instruction_map), "per_bank": per_bank, "opcode_forms": len(forms), "content_file_count": content_manifest["file_count"], "content_aggregate_sha256": content_manifest["aggregate_sha256"]},
        "source_hashes": dict(sorted(source_hashes.items())),
        "execution_policy": {"runtime_opcode_fetch": False, "runtime_opcode_decode": False, "interpreter": False, "emulator_fallback": False, "dispatch_miss": "AI2_V05_STEP_MISSING_IDENTITY/MISSING_BANK", "dynamic_target_miss": "AI2_V05_STEP_BAD_DYNAMIC_TARGET"},
    }
    write_text(out / "generation-receipt.json", json.dumps(receipt, indent=2, sort_keys=True) + "\n")
    final_manifest = ordered_manifest(out, {"generated-tree-manifest.json"})
    write_text(out / "generated-tree-manifest.json", json.dumps(final_manifest, indent=2, sort_keys=True) + "\n")
    return {"identity": identity, "receipt": receipt, "manifest": final_manifest}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", required=True, type=Path, help="external exact Adventure Island II .nes")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    result = generate(args.root.resolve(), args.rom.resolve(), args.output.resolve())
    print(f"{TOOL_NAME} {TOOL_VERSION}: generated {result['identity']['accepted_instruction_contexts']} fixed identities")
    print(f"generated tree: {result['manifest']['file_count']} files aggregate={result['manifest']['aggregate_sha256']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
