# Version 09 public API, snapshots, hooks and frontend boundary

Version 09 wraps the sealed Version 08 Adventure Island II machine behind a small opaque public interface. It does not add executable ROM authority, discover code from gameplay, reinterpret machine state in a frontend, or relax fail-closed execution.

## Opaque ownership

`include/ai2_static_core.h` forward-declares `Ai2StaticCore`; the public header does not expose the V06 runtime, V07 PPU, V08 APU/DMA/controller structures, mapper internals, generated dispatch structures, host callbacks, or owned ROM buffers. `src/api/ai2_v09_core.c` is the private owner and binds the already-certified V06/V07/V08 machine instead of duplicating those semantics.

The exact external ROM is validated against SHA-256 `10dd53fd62c95e77856b09c693b3bdd14b62a89988902eea111257cafe9b8acc`. Accepted PRG/CHR data are copied into core-owned storage. The host cannot substitute instruction bytes after reset.

## One frame path

`ai2_static_core_advance_frame()` is the only public execution advance. It accepts pending Player 1/Player 2 button masks and applies them to the existing V08 live-controller state. The game still obtains controller bits only by its ordinary emulated `$4016/$4017` strobe/latch/serial reads. A bounded instruction limit prevents an unbounded host call, and any unproved generated identity or machine operation still traps fail closed.

Video is copied out as indexed or BGRA pixels. Audio is read from the bounded core PCM ring into caller storage. Frontend code receives no direct machine pointers.

`frontend/common/ai2_frontend_session.c` owns the single adapter used by both the headless and headed-boundary clients. The two clients do not include private runtime/PPU/APU/mapper headers and do not implement alternate schedulers.

## Snapshot format and atomicity

The Version 09 snapshot format is `AI2SNAP1`, file version 1, canonical state version 1. It is not a raw C structure dump. Numeric fields are serialized explicitly in declared little-endian order. The snapshot binds itself to both the exact ROM SHA-256 and the generated V09 core compatibility SHA-256.

The payload records canonical machine state needed to resume generated execution: current physical-bank/PC identity, pending controllers, generated-instruction count, CPU/RAM/mapper/cycle/interrupt/stack/trap state, PPU memory/register/frame state, APU channels/frame sequencer/controllers/DMA/PCM state, and the explicit executable-RAM epoch count (currently zero). It does not serialize host pointers, call stacks, `FILE` objects, callbacks, observation hooks, frontend objects, or transient host buffers.

Load verifies file magic/version, core identity, ROM identity, payload length/hash, trailing-data absence, field bounds and mapper/state consistency into a temporary core before committing. A failed load therefore leaves the live core unchanged.

Save writes a temporary file, flushes and synchronizes it, closes it, rereads and validates it, compares its canonical payload, and only then atomically replaces the destination. The implementation uses POSIX rename semantics on Linux and `MoveFileExA` replacement on Windows.

## Observation hooks

Hooks can filter instruction-before, instruction-after, bus-read, bus-write, frame-complete and frontier events. Hooks are synchronous observation/control surfaces only. They may continue, stop, or call the explicit stop request; they cannot supply an opcode result, change an indirect target, dispatch an unknown PC, mutate mapper authority, or execute fallback logic.

Core API re-entry from a hook is rejected. This includes execution and read-only queries, preventing a callback from using the core as a nested scheduler. Hook registration and transient stop state are deliberately excluded from snapshots.

## Compatibility identity

`tools/generate_v09_core_identity.py` deterministically hashes the exact public/private API sources plus the inherited target machine sources and sealed generated V05 authority. The current compatibility identity is:

`75194b92171c596c8925e41691916e516e94c07d856c24eca71dc5457759bf52`

The complete pin set is recorded in `config/v09-core-contract.json` and emitted as constants in `generated/v09/ai2_v09_identity.h`.

## Construction proof

`tests/construction/test_v09_api_state.c` performs 65 checks covering exact-ROM acceptance/rejection, copied video/audio, controller serial-latch behavior through `$4016`, hook filtering/stop/re-entry rejection, snapshot round-trip identity, and atomic rejection of payload corruption, wrong ROM identity, wrong core identity and trailing data.

The same suite passes GCC, Clang and Clang ASan/UBSan. The GCC gate also links one shared core object set into the headed-boundary and headless clients; both produce the same three-frame result (`PC=E0B5`, physical bank 15, 25,059 generated instructions, frame hash `4c5576c12943c383`, 2,142 available audio samples).

The Version 08 APU/DMA/controller build gate was rerun unchanged and still passes 8,573 checks under GCC, Clang and Clang ASan/UBSan.

## Scope boundary

V09 closes only the public API/persistence/observation/frontend-boundary milestone. It does not close the remaining ROM-wide fixed-point proof or bounded oracle checkpoint agreement. Those remain Version 10 construction responsibilities. Gameplay qualification remains unauthorized until V10 writes `STATIC_CORE_COMPLETE` after all required construction claims pass.
