# Simple mailbox chess engine 

- 6 million node per second move gen
- 250k position evaluation per second 


# Notable Features:

- Iterative deepening
- Null move pruning
- Futility Pruning 
- Late Move Reduction
- Aspiration Windows
- Quiescence Search
- Static Exchange Evaluation for move ordering and qsearch
- 2 killer move trackers     for move ordering
- move history heuristic     for move ordering
- hand drawn sprites :p


# Position Evaluation Features:

- Piece-Square lookup tables 
- Pawn structure (doubled, isolated, protected, passed)
- Piece Mobility 
- King Safety
- Enemy king move restriction for finding mates in endgame
- King to King distance       for finding mates in endgame


# Minor Potential Improvements:

- fail-high and fail-low aspiration window updates
- depth extension for piece sacrifices (engine doesn't properly evaluate rook for knight/bishop to weaken king safety)
- more tapered opening-mid-end evaluation
- trapped piece eval


# Major Potential Improvements:

- Transposition Tables for faster search
- Magic Bitboards      for faster search
- Opening Library
- NNUE position evaluation

- This engine is my v0 proof of concept
- I am working towards a bitboard based engine with possibly NNUE eval


# genAI README below

## Features
- Graphical chess board using SDL2
- Separate engine thread for AI calculations
- Mouse-based piece movement
- Real-time game state display

## Build Instructions

### Prerequisites
- CMake 3.15 or higher
- C++17 compatible compiler
# Chess AI

A C++ chess program with an SDL2-based GUI and a separate engine thread for AI calculations.

This repository contains the engine, GUI, and supporting code used to build a playable chess program on Windows (and other platforms supported by SDL2 and CMake).

## Features
- SDL2 GUI with piece rendering and simple controls
- Separate engine thread for asynchronous AI computation
- Move generation, evaluation, and configurable AI search depth
- Test/executable targets: `ChessAI`, `MoveGen`, `EvaluationTesting`

## Repository layout
- `src/` — implementation files (e.g. `ChessEngine.cpp`, `Board.cpp`, `GUI.cpp`)
- `include/` — public headers (e.g. `Board.h`, `ChessEngine.h`, `GUI.h`)
- `assets/` — images and other runtime assets (copied into the build output by CMake)
- `CMakeLists.txt` — build configuration

## Prerequisites
- CMake 3.15 or newer
- A C++17-capable compiler (MSVC, clang, or gcc)
- vcpkg (recommended on Windows) or system-installed SDL2, SDL2_ttf, SDL2_image

On Windows we recommend using vcpkg to install SDL dependencies and provide the toolchain file to CMake.

### vcpkg (Windows) quick setup
1. Clone or install vcpkg and bootstrap it per vcpkg documentation.
2. Install required libraries (example x64 triplet):

```powershell
vcpkg install sdl2:x64-windows
vcpkg install sdl2-ttf:x64-windows
vcpkg install sdl2-image:x64-windows
```

3. Note the vcpkg toolchain file path, e.g. `C:\vcpkg\scripts\buildsystems\vcpkg.cmake`.

## Build (Windows / vcpkg)

Open a Developer PowerShell or regular terminal and run:

```powershell
cd <repo-root>
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake -A x64
cmake --build . --config Release
```

Notes:
- The build will produce `ChessAI.exe` (and test binaries) under the build output folder. For multi-config generators like Visual Studio, use `--config Release` or `--config Debug` when building or running.
- CMake already copies the `assets/` folder into the build output directory.

## Running

- From the build output directory (example Visual Studio path):

```powershell
.\Release\ChessAI.exe
# or
.\Debug\ChessAI.exe
```

- If you installed libraries with vcpkg, ensure the vcpkg-installed DLLs are discoverable at runtime. The easiest ways are:
	- Run the executable from the build directory where CMake/toolchain placed DLLs.
	- Or copy required DLLs (`SDL2.dll`, `SDL2_ttf.dll`, `SDL2_image.dll`) into the same directory as the executable.

## Running individual targets
- `MoveGen` and `EvaluationTesting` are other executables defined in `CMakeLists.txt` and can be built/run the same way. Use your IDE or call them from the build output folder.

## VS Code / CMake Tools
- Install the CMake Tools extension for best UX in VS Code. It detects kits, configures build folders, and lets you build and run targets from the GUI.
- The workspace includes a tasks.json/launch config to help debugging with the Visual Studio debugger.

## VS Code setup (quick)
1. Install these extensions:
	- `ms-vscode.cmake-tools` (CMake Tools)
	- `ms-vscode.cpptools` (C/C++ IntelliSense and debugging)
2. Open the repository folder in VS Code.
3. Use the CMake status bar at the bottom to select a kit (e.g. `Visual Studio 2022 - x64`) and configure the project.
4. Point CMake to the vcpkg toolchain if you use vcpkg. Add the following to the `CMake: Configure` command (or use the configure input):

```text
-DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
```

5. Build and run targets using the CMake sidebar or the status bar target dropdown. Use the `Run` view or the included `launch.json` to start a debugging session with the Visual Studio debugger.

Tips:
- If IntelliSense can't find SDL headers, ensure the selected kit/compilers match the architecture (x64) and that CMake configuration completed successfully.
- Use `CMake: Clean Reconfigure` from the command palette if build configuration seems stale.

## Common build/run issues
- Missing runtime DLLs (e.g. `SDL2.dll`): ensure vcpkg integration or copy the DLLs to the executable folder.
- Architecture mismatch (DLLs built for x86 while binary is x64): ensure you use the same triplet (e.g. `x64-windows`) in vcpkg and pass `-A x64` to CMake when using MSVC.
- If the GUI fails to find assets (textures, fonts), confirm the `assets` folder exists next to the executable — CMake copies it automatically for common generators.

## Development notes
- Key files:
	- `include/Board.h`, `src/Board.cpp` — board representation, move making/unmaking, FEN load/save
	- `include/ChessEngine.h`, `src/ChessEngine.cpp` — search/evaluation and engine loop
	- `include/GUI.h`, `src/GUI.cpp` — SDL rendering and input handling
	- `include/Move.h`, `src/Move.cpp` — move representation

- AI tuning and defaults:
	- Default search depth is set in `ChessEngine` constructor (`aiSearchDepth`). Adjust as desired.
	- The engine exposes `CommandData` and `ResponseData` structs used by the GUI to send commands and receive AI responses.

## Troubleshooting and testing
- Perft/move-generation issues: there are dedicated test targets (`MoveGen`) that can help debug move generation.
- If you hit crashes or wrong behavior, try running in `Debug` mode with sanitizers (if available) or instrument the engine logging.

## Contributing
- Feel free to open issues or submit patches. Clean, focused PRs with small, testable changes are preferred.

## License
This project is licensed under the MIT License — see [LICENSE](LICENSE) for full terms.
