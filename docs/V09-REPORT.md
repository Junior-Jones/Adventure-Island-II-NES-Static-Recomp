# Adventure Island II NES Static Recomp — Version 09 completion report

Version 09 is complete for its defined Starter NES(4) scope. It adds the opaque public core boundary, canonical ROM/core-bound atomic snapshots, filtered non-reentrant observation hooks, copied video/audio and a shared headed/headless frame adapter without adding gameplay-derived authority or an interpreter/emulator fallback.

The public API hides the V06/V07/V08 machine layout behind `Ai2StaticCore`. The exact external ROM is copied only after SHA-256 qualification. There is one public `advance_frame` execution path. Host button masks enter the existing V08 controller model and the game receives them only through its normal `$4016/$4017` serial controller reads.

Snapshots use explicit little-endian canonical fields rather than a struct dump. They bind to the ROM and V09 compatibility identity `75194b92171c596c8925e41691916e516e94c07d856c24eca71dc5457759bf52`. Save uses temporary-write, flush/sync, reread/validation and atomic replacement. Load validates a temporary state before committing. Corrupt payload, wrong ROM identity, wrong core identity and trailing-data tests all prove failure atomicity.

Observation hooks are filtered, synchronous and non-reentrant. They can observe or request a stop, but cannot provide execution semantics, targets or fallback results. The frontend cannot inspect private machine fields and both headed-boundary and headless clients call the same common frame adapter.

The final V09 functional suite passes 65 checks under GCC, Clang and Clang ASan/UBSan. The shared headed/headless comparison passes for three frames with matching final PC/bank/instruction count/frame hash/audio availability. The V09 contract/static-source audit passes all 17 pinned core inputs. The inherited V08 static-demand/source/machine-contract checks pass, and the V08 machine suite still passes 8,573 checks under GCC, Clang and sanitizer.

No gameplay, screenshots, emulator traces or oracle-observed PCs were used to construct or expand static authority. No runtime opcode fetch/decode, 6502 interpreter or emulator fallback was introduced. V10 still owns ROM-wide fixed-point closure, remaining bounded cold-boot oracle checkpoint agreement, final construction audit and the only valid `STATIC_CORE_COMPLETE` claim.
