# Build and Setup Guide

## Prerequisites

### 1. Install CMake
- Download from: https://cmake.org/download/
- Version 3.15 or higher required
- Add to PATH during installation

### 2. Install a C++ Compiler

#### Windows - Visual Studio
- Download Visual Studio 2019 or 2022 Community (free)
- During installation, select "Desktop development with C++"
- This includes MSVC compiler and build tools

#### Alternative: MinGW
- Download from: https://www.mingw-w64.org/
- Or use MSYS2: https://www.msys2.org/

### 3. Install vcpkg (Package Manager)

```powershell
# Clone vcpkg
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
cd C:\vcpkg

# Bootstrap vcpkg
.\bootstrap-vcpkg.bat

# Add to environment (optional but recommended)
[System.Environment]::SetEnvironmentVariable('VCPKG_ROOT', 'C:\vcpkg', 'User')
```

### 4. Install SDL2 Libraries

```powershell
# Navigate to vcpkg directory
cd C:\vcpkg

# Install SDL2 for 64-bit Windows
.\vcpkg install sdl2:x64-windows
.\vcpkg install sdl2-ttf:x64-windows

# For 32-bit Windows, use:
# .\vcpkg install sdl2:x86-windows
# .\vcpkg install sdl2-ttf:x86-windows

# Integrate with Visual Studio
.\vcpkg integrate install
```

## Building the Project

### Option 1: Using CMake GUI

1. Open CMake GUI
2. Set source directory to: `c:\Users\clanc\VSCode\chess_ai`
3. Set build directory to: `c:\Users\clanc\VSCode\chess_ai\build`
4. Click "Configure"
5. Select your generator (e.g., "Visual Studio 17 2022")
6. Set CMAKE_TOOLCHAIN_FILE to: `C:\vcpkg\scripts\buildsystems\vcpkg.cmake`
7. Click "Generate"
8. Click "Open Project" to open in Visual Studio
9. Build with F7 or Build > Build Solution

### Option 2: Using Command Line

```powershell
# Navigate to project directory
cd c:\Users\clanc\VSCode\chess_ai

# Create build directory
mkdir build
cd build

# Configure with CMake (using vcpkg toolchain)
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake

# Build the project
cmake --build . --config Release

# Or for Debug build:
cmake --build . --config Debug
```

### Option 3: Using VS Code

1. Install VS Code extensions:
   - C/C++ (Microsoft)
   - CMake Tools (Microsoft)
   
2. Open the project folder in VS Code

3. Press `Ctrl+Shift+P` and run:
   - "CMake: Configure" (select your kit)
   - "CMake: Build"

4. Set CMAKE_TOOLCHAIN_FILE in VS Code settings:
   ```json
   {
       "cmake.configureSettings": {
           "CMAKE_TOOLCHAIN_FILE": "C:/vcpkg/scripts/buildsystems/vcpkg.cmake"
       }
   }
   ```

## Running the Program

### From Command Line
```powershell
# Navigate to build directory
cd c:\Users\clanc\VSCode\chess_ai\build

# Run the executable
.\Debug\ChessAI.exe
# or
.\Release\ChessAI.exe
```

### From Visual Studio
- Press F5 to run with debugging
- Or Ctrl+F5 to run without debugging

### From VS Code
- Press F5 after configuring CMake
- Or use "CMake: Run Without Debugging"

## Troubleshooting

### SDL2 Not Found
- Ensure vcpkg installed SDL2 correctly
- Check that CMAKE_TOOLCHAIN_FILE is set correctly
- Try running: `vcpkg integrate install`

### Font Not Loading
- The program uses `C:\Windows\Fonts\arial.ttf` by default
- If you get a font warning, the program will still run (pieces shown as letters)
- You can modify the font path in `GUI.cpp` line ~73

### Linking Errors
- Ensure you're building for the correct architecture (x64 vs x86)
- Rebuild vcpkg packages if needed
- Clean build directory and reconfigure

### Missing DLLs at Runtime
- Copy SDL2.dll and SDL2_ttf.dll to the executable directory
- They should be in: `C:\vcpkg\installed\x64-windows\bin\`
- Or add vcpkg bin directory to PATH

## Project Structure

```
chess_ai/
├── include/          # Header files
│   ├── Board.h
│   ├── ChessEngine.h
│   ├── GUI.h
│   ├── InputHandler.h
│   ├── Move.h
│   └── Piece.h
├── src/              # Implementation files
│   ├── Board.cpp
│   ├── ChessEngine.cpp
│   ├── GUI.cpp
│   ├── InputHandler.cpp
│   ├── main.cpp
│   ├── Move.cpp
│   └── Piece.cpp
├── assets/           # Images and resources (optional)
├── build/            # Build output (generated)
├── CMakeLists.txt    # Build configuration
└── README.md         # Project overview
```

## Next Steps

This framework provides:
- ✅ Threaded architecture (GUI + Engine separation)
- ✅ Basic move handling
- ✅ Board representation
- ✅ Mouse input system
- ✅ SDL2 rendering

To extend it:
1. Complete move validation logic in `Board.cpp`
2. Implement full piece movement rules (castling, en passant, etc.)
3. Add AI with minimax algorithm in `ChessEngine.cpp`
4. Replace text pieces with sprite images
5. Add move history and notation
6. Implement game save/load
7. Add difficulty levels
8. Implement opening book

Happy coding!
