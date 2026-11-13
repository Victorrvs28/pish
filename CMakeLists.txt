cmake_minimum_required(VERSION 3.16)

project(pish
    VERSION 1.0.0
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SOURCES
    src/main.c
    src/lexer/lexer.c
    src/parser/parser.c
    src/transpiler/transpiler.cpp
)

add_executable(pish ${SOURCES})
target_include_directories(pish PRIVATE include)
