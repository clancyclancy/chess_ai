cmake_minimum_required(VERSION 3.15)
project(ChessAI VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -----------------------------------------
# Custom "Profile" build type
# -----------------------------------------
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo;Profile" CACHE STRING "" FORCE)

# Compiler flags for Profile build
set(CMAKE_CXX_FLAGS_PROFILE "/Zi /Od /Ob0 /Oy- /RTC- /GS-")
set(CMAKE_C_FLAGS_PROFILE   "/Zi /Od /Ob0 /Oy- /RTC- /GS-")

# Linker flags for Profile build
set(CMAKE_EXE_LINKER_FLAGS_PROFILE "/DEBUG")
set(CMAKE_SHARED_LINKER_FLAGS_PROFILE "/DEBUG")

# Define PROFILE_BUILD macro for conditional noinline
add_compile_definitions($<$<CONFIG:Profile>:PROFILE_BUILD>)

# -----------------------------------------
# Dependencies
# -----------------------------------------
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_ttf CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/src)

# -----------------------------------------
# Sources
# -----------------------------------------
set(SOURCES
    src/main.cpp
    src/Board.cpp
    src/Piece.cpp
    src/Move.cpp
    src/ChessEngine.cpp
    src/GUI.cpp
    src/InputHandler.cpp
)

add_executable(ChessAI ${SOURCES})

target_link_libraries(ChessAI
    PRIVATE
    SDL2::SDL2main
    SDL2::SDL2
    SDL2_ttf::SDL2_ttf
    SDL2_image::SDL2_image
)

file(COPY ${CMAKE_SOURCE_DIR}/assets DESTINATION ${CMAKE_BINARY_DIR})

# -----------------------------------------
# Additional executables
# -----------------------------------------
add_executable(MoveGen
    src/MoveGenerationTesting.cpp
    src/Board.cpp
    src/Move.cpp
    src/Piece.cpp
    src/ChessEngine.cpp
)

add_executable(EvaluationTesting
    src/EvaluationTesting.cpp
    src/Board.cpp
    src/Piece.cpp
    src/Move.cpp
    src/ChessEngine.cpp
)

# UCI front-end for the testbed match runner (testbed/run_match.py) and chess GUIs.
# No SDL dependency: it only needs the engine core.
add_executable(ChessUCI
    src/uci_main.cpp
    src/Board.cpp
    src/Piece.cpp
    src/Move.cpp
    src/ChessEngine.cpp
)

if(WIN32)
    # timeBeginPeriod: 1ms sleep granularity so bestmove isn't delayed ~16ms
    target_link_libraries(ChessUCI PRIVATE winmm)
endif()

# -----------------------------------------
# Apply Profile flags to all executables
# -----------------------------------------
foreach(target ChessAI MoveGen EvaluationTesting ChessUCI)
    target_compile_options(${target} PRIVATE 
        $<$<CONFIG:Profile>:/Zi /Od /Ob0 /Oy- /RTC- /GS->
    )
    target_link_options(${target} PRIVATE 
        $<$<CONFIG:Profile>:/DEBUG>
    )
endforeach()

# -----------------------------------------
# Asset copying
# -----------------------------------------
add_custom_command(
    TARGET ChessAI POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:ChessAI>/assets
)
