# Adventure Island II (NES) Static Recomp 1.0.0

Native Windows frontend and game-specific static core for Adventure Island II on the Nintendo Entertainment System.

The original game ROM is not included. Supply a legally obtained copy with this exact name and SHA-256 hash:

- `Adventure Island II (USA).nes`: `10dd53fd62c95e77856b09c693b3bdd14b62a89988902eea111257cafe9b8acc`

## Running the application

Place the ROM in the `Rom` folder beside `Launcher.exe`, then run `Launcher.exe`. You can also use Browse ROM to select the verified image elsewhere. The application validates the ROM before enabling Run. The original NES file is read-only; quick-save state is stored separately in `Snapshots`.

Escape switches between the running game and Launcher. The Launcher provides controls for audio, video, input bindings, quick-save snapshots and game-window screenshots. Press F1 in the Launcher for the complete shortcut guide.

The first launch displays the Welcome window by itself. Close it with Escape or the Close button to open the Launcher.

## What "static recompilation" means

The game's executable paths were analysed before the application was built and emitted as a fixed generated C catalog of 16,544 exact physical-bank and program-counter identities. Compact metadata retains every identity's physical bank, expected PC, statically selected semantic operation, operand, legal successors and target classification. One native exact-identity dispatcher executes only records admitted by that catalog.

The production runtime does not fetch arbitrary guest opcodes into a general-purpose interpreter loop and has no dynamic recompiler, JIT compiler, learning system, Mesen process or emulator fallback. Interrupt handling remains inside the fixed generated execution authority. Unknown identities, mapper states or targets stop with a diagnostic error instead of switching to fallback execution.

The runtime includes the native NES CPU bus, PPU graphics, APU audio, DMA, controller input and Sharp MMC3B mapper behavior. Gameplay, graphics and sound remain live and respond to player input; they are not prerecorded.

Development tests and reference comparisons are used while building the project. They are not included in or used by the production runtime.

## Building from source on Windows

Requirements:

- 64-bit Windows 10 or later
- Visual Studio 2022 with Desktop development with C++
- CMake 3.20 or newer
- Internet access during initial configuration so CMake can fetch the pinned SDL3 source archive

From the `Source code` folder, run:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target adventure-island-ii-launcher
```

The current configuration fetches the pinned SDL3 source, builds it statically and produces one distributable application, `Launcher.exe`. `Launcher.exe` starts another instance of itself in game mode, so no second game executable or SDL DLL is required. The executable and runtime files are written to `build/Release`.

Version 1.0.0 contains the compact generated NES game-code authority, native NES hardware runtime, exact-ROM validation, snapshots and fail-closed dispatch. The separate historical Starter certification status is documented in `docs/STATIC-CORE-STATUS.md`.
