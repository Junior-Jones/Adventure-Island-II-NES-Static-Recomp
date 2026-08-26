#!/usr/bin/env python3
"""Adventure Island II V03 ROM-wide PRG inventory and conservative root disassembly.

V03 deliberately does not perform V04 control-flow closure. Every PRG byte begins
as unclassified data. RESET/NMI/IRQ are rooted in the fixed MMC3 reset mapping and
only straight-line bytes through the first control-transfer instruction are admitted
as code. Transfer edges are emitted as V04 seeds instead of being followed here.
No ROM bytes are copied into generated project artifacts.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import shutil
import tempfile
from collections import Counter, defaultdict
from pathlib import Path

TOOL_NAME = "ai2-static-disassembler"
TOOL_VERSION = "0.3.0-v03"
ROOT_NAMES = ("reset", "nmi", "irq")
BRANCHES = {"BPL", "BMI", "BVC", "BVS", "BCC", "BCS", "BNE", "BEQ"}
RETURNS = {"RTS", "RTI"}
TERMINATORS = BRANCHES | RETURNS | {"JMP", "JSR", "BRK", "STP"}


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def hx(value: int, width: int = 4) -> str:
    return f"0x{value:0{width}X}"


def write_csv(path: Path, fieldnames: list[str], rows: list[dict]) -> None:
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames, lineterminator="\n")
        w.writeheader()
        w.writerows(rows)


def flow_kind(mnemonic: str, mode: str) -> str:
    if mnemonic in BRANCHES:
        return "conditional_branch"
    if mnemonic == "JSR":
        return "call"
    if mnemonic == "JMP":
        return "indirect_jump" if mode == "Ind" else "direct_jump"
    if mnemonic == "RTS":
        return "return"
    if mnemonic == "RTI":
        return "interrupt_return"
    if mnemonic == "BRK":
        return "software_interrupt"
    if mnemonic == "STP":
        return "cpu_stop"
    return "fallthrough"


def direct_target(inst: bytes, pc: int, mnemonic: str, mode: str) -> int | None:
    if mnemonic in BRANCHES and len(inst) == 2:
        rel = inst[1] if inst[1] < 0x80 else inst[1] - 0x100
        return (pc + 2 + rel) & 0xFFFF
    if mnemonic in {"JMP", "JSR"} and mode == "Abs" and len(inst) == 3:
        return inst[1] | (inst[2] << 8)
    return None


def fixed_bank_for_cpu(pc: int, last_bank: int) -> int | None:
    return last_bank if 0xE000 <= pc <= 0xFFFF else None


def bank_offset_for_cpu(pc: int) -> int:
    return pc & 0x1FFF


def build_inventory(project: Path, rom_path: Path, out_dir: Path) -> dict:
    profile = load_json(project / "config/target-profile.json")
    mapper = load_json(project / "config/mapper-contract.json")
    opmeta = load_json(project / "config/2a03-opcodes.json")

    if opmeta.get("entry_count") != 256 or len(opmeta.get("entries", [])) != 256:
        raise SystemExit("opcode metadata must contain exactly 256 entries")
    opcodes = {int(e["opcode"]): e for e in opmeta["entries"]}
    if set(opcodes) != set(range(256)):
        raise SystemExit("opcode metadata is not a complete 0x00-0xFF map")

    rom = rom_path.read_bytes()
    expected = profile["rom"]
    if len(rom) != expected["size_bytes"]:
        raise SystemExit(f"ROM size mismatch: got {len(rom)}, expected {expected['size_bytes']}")
    if sha256_bytes(rom) != expected["file_sha256"]:
        raise SystemExit("ROM SHA-256 mismatch")
    if rom[:4] != b"NES\x1a":
        raise SystemExit("invalid iNES signature")
    header_bytes = int(expected["header_bytes"])
    prg_size = int(expected["prg_rom_bytes"])
    chr_size = int(expected["chr_rom_bytes"])
    prg = rom[header_bytes:header_bytes + prg_size]
    payload = rom[header_bytes:header_bytes + prg_size + chr_size]
    if sha256_bytes(payload) != expected["payload_sha256"]:
        raise SystemExit("ROM payload SHA-256 mismatch")

    bank_size = int(mapper["geometry"]["prg_bank_bytes"])
    bank_count = int(mapper["geometry"]["prg_bank_count_8k"])
    last_bank = int(mapper["geometry"]["fixed_last_prg_bank"])
    if len(prg) != bank_size * bank_count:
        raise SystemExit("PRG geometry mismatch")

    # Every byte begins as unclassified data. No byte is ever called padding in V03.
    primary = ["unclassified"] * len(prg)
    primary_evidence = ["rom-wide initial data classification"] * len(prg)
    instruction_bytes: dict[int, tuple[int, str]] = {}
    instructions: dict[tuple[int, int], dict] = {}
    root_paths: dict[str, list[tuple[int, int]]] = defaultdict(list)
    decode_conflicts: list[dict] = []
    overlap_conflicts: list[dict] = []
    flow_rows: list[dict] = []

    vectors = {name: int(profile["vectors"][name], 16) for name in ROOT_NAMES}
    for root in ROOT_NAMES:
        pc = vectors[root]
        bank = fixed_bank_for_cpu(pc, last_bank)
        if bank is None:
            raise SystemExit(f"{root} vector {pc:04X} is not in fixed $E000-$FFFF mapping")
        seen = set()
        while True:
            identity = (bank, bank_offset_for_cpu(pc))
            if identity in seen:
                decode_conflicts.append({
                    "kind": "straight_line_loop",
                    "root": root,
                    "physical_prg_bank": bank,
                    "bank_offset": hx(identity[1]),
                    "cpu_pc": hx(pc),
                    "detail": "V03 straight-line walk revisited an instruction identity",
                })
                break
            seen.add(identity)
            root_paths[root].append(identity)

            abs_off = bank * bank_size + identity[1]
            opcode = prg[abs_off]
            meta = opcodes[opcode]
            size = int(meta["size"])
            if identity[1] + size > bank_size:
                decode_conflicts.append({
                    "kind": "instruction_crosses_physical_bank",
                    "root": root,
                    "physical_prg_bank": bank,
                    "bank_offset": hx(identity[1]),
                    "cpu_pc": hx(pc),
                    "detail": f"decoded size {size} crosses 8 KiB bank boundary",
                })
                break
            inst = prg[abs_off:abs_off + size]
            kind = flow_kind(meta["mnemonic"], meta["mode"])
            target = direct_target(inst, pc, meta["mnemonic"], meta["mode"])

            existing = instructions.get(identity)
            row = {
                "physical_prg_bank": bank,
                "bank_offset": hx(identity[1]),
                "cpu_pc": hx(pc),
                "length": size,
                "mnemonic": meta["mnemonic"],
                "addressing_mode": meta["mode"],
                "unofficial_opcode": "yes" if meta["unofficial"] else "no",
                "flow_kind": kind,
                "instruction_sha256": sha256_bytes(inst),
                "root_origins": root,
                "mapping_authority": "fixed-last-bank-15",
            }
            if existing:
                if any(existing[k] != row[k] for k in ("length", "mnemonic", "addressing_mode", "instruction_sha256")):
                    decode_conflicts.append({
                        "kind": "identity_decode_conflict",
                        "root": root,
                        "physical_prg_bank": bank,
                        "bank_offset": hx(identity[1]),
                        "cpu_pc": hx(pc),
                        "detail": "same physical identity decoded differently",
                    })
                    break
                origins = sorted(set(existing["root_origins"].split(";")) | {root})
                existing["root_origins"] = ";".join(origins)
            else:
                instructions[identity] = row

            for i in range(size):
                byte_off = abs_off + i
                owner = instruction_bytes.get(byte_off)
                this_owner = (abs_off, f"PRG{bank:02d}:{identity[1]:04X}")
                if owner and owner[0] != abs_off:
                    overlap_conflicts.append({
                        "physical_prg_bank": byte_off // bank_size,
                        "bank_offset": hx(byte_off % bank_size),
                        "first_instruction": owner[1],
                        "second_instruction": this_owner[1],
                        "detail": "accepted instructions overlap in physical PRG bytes",
                    })
                else:
                    instruction_bytes[byte_off] = this_owner
                    primary[byte_off] = "code"
                    primary_evidence[byte_off] = "vector-root straight-line instruction"

            if kind != "fallthrough":
                source_id = f"PRG{bank:02d}:{identity[1]:04X}@{pc:04X}"
                if kind == "conditional_branch":
                    assert target is not None
                    fallthrough = (pc + size) & 0xFFFF
                    for edge_kind, dest in (("branch_taken", target), ("branch_fallthrough", fallthrough)):
                        dest_bank = fixed_bank_for_cpu(dest, last_bank)
                        flow_rows.append({
                            "source_identity": source_id,
                            "source_flow_kind": kind,
                            "edge_kind": edge_kind,
                            "target_cpu_pc": hx(dest),
                            "target_physical_prg_bank_if_fixed": "" if dest_bank is None else dest_bank,
                            "target_bank_offset_if_fixed": "" if dest_bank is None else hx(bank_offset_for_cpu(dest)),
                            "v04_status": "pending",
                            "reason": "direct control-flow following begins in V04",
                        })
                elif kind in {"direct_jump", "call"}:
                    assert target is not None
                    dest_bank = fixed_bank_for_cpu(target, last_bank)
                    flow_rows.append({
                        "source_identity": source_id,
                        "source_flow_kind": kind,
                        "edge_kind": "jump_target" if kind == "direct_jump" else "call_target",
                        "target_cpu_pc": hx(target),
                        "target_physical_prg_bank_if_fixed": "" if dest_bank is None else dest_bank,
                        "target_bank_offset_if_fixed": "" if dest_bank is None else hx(bank_offset_for_cpu(target)),
                        "v04_status": "pending",
                        "reason": "direct control-flow following begins in V04",
                    })
                    if kind == "call":
                        cont = (pc + size) & 0xFFFF
                        cont_bank = fixed_bank_for_cpu(cont, last_bank)
                        flow_rows.append({
                            "source_identity": source_id,
                            "source_flow_kind": kind,
                            "edge_kind": "post_return_continuation",
                            "target_cpu_pc": hx(cont),
                            "target_physical_prg_bank_if_fixed": "" if cont_bank is None else cont_bank,
                            "target_bank_offset_if_fixed": "" if cont_bank is None else hx(bank_offset_for_cpu(cont)),
                            "v04_status": "pending",
                            "reason": "return/control-flow proof begins in V04",
                        })
                else:
                    flow_rows.append({
                        "source_identity": source_id,
                        "source_flow_kind": kind,
                        "edge_kind": "dynamic_or_implicit",
                        "target_cpu_pc": "",
                        "target_physical_prg_bank_if_fixed": "",
                        "target_bank_offset_if_fixed": "",
                        "v04_status": "pending",
                        "reason": "return/interrupt/indirect target requires V04 proof",
                    })
                break

            pc = (pc + size) & 0xFFFF
            next_bank = fixed_bank_for_cpu(pc, last_bank)
            if next_bank != bank:
                decode_conflicts.append({
                    "kind": "fallthrough_leaves_fixed_mapping",
                    "root": root,
                    "physical_prg_bank": bank,
                    "bank_offset": hx(bank_offset_for_cpu(pc)),
                    "cpu_pc": hx(pc),
                    "detail": "V03 does not infer switchable mapping for fallthrough",
                })
                break

    # Proven vector table and its three pointer entries.
    vector_base_pc = 0xFFFA
    vector_base_abs = last_bank * bank_size + bank_offset_for_cpu(vector_base_pc)
    for i in range(6):
        off = vector_base_abs + i
        if primary[off] != "unclassified":
            overlap_conflicts.append({
                "physical_prg_bank": off // bank_size,
                "bank_offset": hx(off % bank_size),
                "first_instruction": primary[off],
                "second_instruction": "vector",
                "detail": "primary classification overlap",
            })
        primary[off] = "vector"
        primary_evidence[off] = "6502 NMI/RESET/IRQ vector table"

    # One strongly evidenced text range: the cartridge's embedded title signature.
    title_signature = b"ADVENTURE ISLE 2"
    title_pos = prg.find(title_signature)
    title_rows = []
    if title_pos >= 0 and prg.find(title_signature, title_pos + 1) < 0:
        for i in range(len(title_signature)):
            off = title_pos + i
            if primary[off] != "unclassified":
                overlap_conflicts.append({
                    "physical_prg_bank": off // bank_size,
                    "bank_offset": hx(off % bank_size),
                    "first_instruction": primary[off],
                    "second_instruction": "text",
                    "detail": "primary classification overlap",
                })
            primary[off] = "text"
            primary_evidence[off] = "unique embedded cartridge title signature"
        title_rows.append({
            "category": "text",
            "subkind": "embedded_title_signature",
            "physical_prg_bank": title_pos // bank_size,
            "start_bank_offset": hx(title_pos % bank_size),
            "end_bank_offset_inclusive": hx((title_pos + len(title_signature) - 1) % bank_size),
            "length": len(title_signature),
            "content_sha256": sha256_bytes(title_signature),
            "evidence": "unique contiguous uppercase ASCII title signature; content not copied to generated ledger",
        })

    # Primary classification ranges, guaranteed to cover every PRG byte exactly once.
    class_rows = []
    start = 0
    for i in range(1, len(prg) + 1):
        boundary = i == len(prg) or primary[i] != primary[start] or (i // bank_size) != (start // bank_size)
        if boundary:
            bank = start // bank_size
            end = i - 1
            class_rows.append({
                "physical_prg_bank": bank,
                "start_bank_offset": hx(start % bank_size),
                "end_bank_offset_inclusive": hx(end % bank_size),
                "length": end - start + 1,
                "primary_class": primary[start],
                "evidence": primary_evidence[start],
            })
            start = i

    # Secondary evidence may overlap the primary vector class by design.
    data_rows = [
        {
            "category": "table",
            "subkind": "interrupt_vector_table",
            "physical_prg_bank": last_bank,
            "start_bank_offset": hx(0x1FFA),
            "end_bank_offset_inclusive": hx(0x1FFF),
            "length": 6,
            "content_sha256": sha256_bytes(prg[vector_base_abs:vector_base_abs + 6]),
            "evidence": "architecturally fixed NMI/RESET/IRQ vector table",
        }
    ]
    for index, name in enumerate(("nmi", "reset", "irq")):
        start_off = vector_base_abs + index * 2
        data_rows.append({
            "category": "pointer",
            "subkind": f"{name}_vector_pointer",
            "physical_prg_bank": last_bank,
            "start_bank_offset": hx(start_off % bank_size),
            "end_bank_offset_inclusive": hx((start_off + 1) % bank_size),
            "length": 2,
            "content_sha256": sha256_bytes(prg[start_off:start_off + 2]),
            "evidence": f"architectural {name.upper()} vector points to {hx(vectors[name])}",
        })
    data_rows.extend(title_rows)
    data_rows.append({
        "category": "graphics_reference",
        "subkind": "none_proven_in_v03",
        "physical_prg_bank": "",
        "start_bank_offset": "",
        "end_bank_offset_inclusive": "",
        "length": 0,
        "content_sha256": "",
        "evidence": "not guessed: graphics-reference use requires later control/data-flow proof",
    })

    instruction_rows = [instructions[k] for k in sorted(instructions)]
    bank_rows = []
    for bank in range(bank_count):
        s = bank * bank_size
        e = s + bank_size
        counts = Counter(primary[s:e])
        bank_rows.append({
            "physical_prg_bank": bank,
            "size_bytes": bank_size,
            "prg_offset_start": hx(s, 5),
            "prg_offset_end_inclusive": hx(e - 1, 5),
            "sha256": sha256_bytes(prg[s:e]),
            "accepted_instruction_count": sum(1 for r in instruction_rows if int(r["physical_prg_bank"]) == bank),
            "code_bytes": counts.get("code", 0),
            "vector_bytes": counts.get("vector", 0),
            "text_bytes": counts.get("text", 0),
            "unclassified_bytes": counts.get("unclassified", 0),
            "padding_bytes": 0,
        })

    roots_rows = []
    for name in ROOT_NAMES:
        pc = vectors[name]
        roots_rows.append({
            "root": name,
            "cpu_pc": hx(pc),
            "physical_prg_bank": last_bank,
            "bank_offset": hx(bank_offset_for_cpu(pc)),
            "accepted_straight_line_instruction_count": len(root_paths[name]),
            "policy": "decode through first control-transfer instruction; do not follow edge until V04",
        })

    temp_parent = out_dir.parent
    temp_parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="v03-build-", dir=temp_parent) as td:
        tmp = Path(td)
        write_csv(tmp / "instruction-ledger.csv",
                  ["physical_prg_bank", "bank_offset", "cpu_pc", "length", "mnemonic", "addressing_mode",
                   "unofficial_opcode", "flow_kind", "instruction_sha256", "root_origins", "mapping_authority"],
                  instruction_rows)
        write_csv(tmp / "classification-ranges.csv",
                  ["physical_prg_bank", "start_bank_offset", "end_bank_offset_inclusive", "length", "primary_class", "evidence"],
                  class_rows)
        write_csv(tmp / "data-evidence.csv",
                  ["category", "subkind", "physical_prg_bank", "start_bank_offset", "end_bank_offset_inclusive", "length", "content_sha256", "evidence"],
                  data_rows)
        write_csv(tmp / "control-flow-seeds.csv",
                  ["source_identity", "source_flow_kind", "edge_kind", "target_cpu_pc", "target_physical_prg_bank_if_fixed",
                   "target_bank_offset_if_fixed", "v04_status", "reason"],
                  flow_rows)
        write_csv(tmp / "decode-conflicts.csv",
                  ["kind", "root", "physical_prg_bank", "bank_offset", "cpu_pc", "detail"],
                  decode_conflicts)
        write_csv(tmp / "overlap-conflicts.csv",
                  ["physical_prg_bank", "bank_offset", "first_instruction", "second_instruction", "detail"],
                  overlap_conflicts)
        write_csv(tmp / "roots.csv",
                  ["root", "cpu_pc", "physical_prg_bank", "bank_offset", "accepted_straight_line_instruction_count", "policy"],
                  roots_rows)
        (tmp / "prg-bank-inventory.json").write_text(json.dumps({
            "schema": 1,
            "tool": {"name": TOOL_NAME, "version": TOOL_VERSION},
            "rom_file_sha256": expected["file_sha256"],
            "prg_sha256": sha256_bytes(prg),
            "prg_bytes": len(prg),
            "bank_size": bank_size,
            "bank_count": bank_count,
            "banks": bank_rows,
        }, indent=2) + "\n", encoding="utf-8")

        counts = Counter(primary)
        generated_files = sorted(p for p in tmp.iterdir() if p.is_file())
        file_hashes = {p.name: sha256_file(p) for p in generated_files}
        summary = {
            "schema": 1,
            "milestone": "V03",
            "tool": {"name": TOOL_NAME, "version": TOOL_VERSION},
            "rom_file_sha256": expected["file_sha256"],
            "payload_sha256": expected["payload_sha256"],
            "prg_sha256": sha256_bytes(prg),
            "physical_prg_banks": bank_count,
            "prg_bytes_covered_exactly_once_by_primary_classification": len(prg),
            "primary_classification_counts": dict(sorted(counts.items())),
            "padding_classification_count": 0,
            "accepted_instruction_count": len(instruction_rows),
            "accepted_instruction_bytes": counts.get("code", 0),
            "roots": {r["root"]: r["accepted_straight_line_instruction_count"] for r in roots_rows},
            "control_flow_seed_count": len(flow_rows),
            "decode_conflict_count": len(decode_conflicts),
            "overlap_conflict_count": len(overlap_conflicts),
            "proven_data_categories": {
                "table_records": sum(1 for r in data_rows if r["category"] == "table"),
                "pointer_records": sum(1 for r in data_rows if r["category"] == "pointer"),
                "vector_primary_bytes": counts.get("vector", 0),
                "text_records": sum(1 for r in data_rows if r["category"] == "text"),
                "graphics_reference_records": sum(1 for r in data_rows if r["category"] == "graphics_reference" and int(r["length"]) > 0),
            },
            "graphics_reference_status": "none proven in V03; unresolved bytes remain unclassified rather than guessed",
            "v04_not_performed": True,
            "production_lowering_performed": False,
            "gameplay_used": False,
            "generated_file_sha256_excluding_summary": file_hashes,
        }
        (tmp / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

        if out_dir.exists():
            shutil.rmtree(out_dir)
        shutil.copytree(tmp, out_dir)

    return summary


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("rom", type=Path, help="exact external Adventure Island II .nes image")
    ap.add_argument("--out", type=Path, help="output directory; defaults to generated/v03")
    ap.add_argument("--project-root", type=Path, help="project root; defaults to parent of tools directory")
    args = ap.parse_args()
    project = (args.project_root or Path(__file__).resolve().parents[1]).resolve()
    out = (args.out or project / "generated/v03").resolve()
    summary = build_inventory(project, args.rom.resolve(), out)
    print(
        f"PASS: {TOOL_NAME} {TOOL_VERSION}; "
        f"banks={summary['physical_prg_banks']} bytes={summary['prg_bytes_covered_exactly_once_by_primary_classification']} "
        f"instructions={summary['accepted_instruction_count']} flow_seeds={summary['control_flow_seed_count']} "
        f"decode_conflicts={summary['decode_conflict_count']} overlaps={summary['overlap_conflict_count']}"
    )
    return 0 if summary["decode_conflict_count"] == 0 and summary["overlap_conflict_count"] == 0 else 3


if __name__ == "__main__":
    raise SystemExit(main())
