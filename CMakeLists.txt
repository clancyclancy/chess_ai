cmake_minimum_required(VERSION 3.15)
project(ChessAI VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find SDL2 and SDL2_ttf via vcpkg
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_ttf CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)  

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/src)

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


add_executable(MoveGen
    src/MoveGenerationTesting.cpp
    src/Board.cpp
    src/Move.cpp
    src/Piece.cpp
    src/ChessEngine.cpp
    )

# Copy assets folder to the output directory 
add_custom_command(
    TARGET ChessAI POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:ChessAI>/assets
)

add_executable(EvaluationTesting 
    src/EvaluationTesting.cpp
    src/Board.cpp
    src/Piece.cpp
    src/Move.cpp
    src/ChessEngine.cpp
    )

# only for debug names in release build
# set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Zi /DNDEBUG")
# set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/DEBUG")
