cmake_minimum_required(VERSION 3.28)
project(mattc C CXX ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(build-etc/macros.cmake)

find_library(MALLOC NAMES jemalloc tcmalloc)

find_package(LLVM REQUIRED CONFIG)
find_package(Doxygen)
find_package(GTest REQUIRED)
find_package(benchmark REQUIRED)
find_package(glm REQUIRED)

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

# Possibly enable ASAN/TSAN/etc
option(ASAN "address sanitizer" OFF)
option(UBSAN "undefined behavior sanitizer" OFF)
option(TSAN "thread sanitizer" OFF)
option(MSAN "memory sanitizer" OFF)

# Enable debugging symbols.
option(DEBUG_SYMBOLS "build with debug symbols" ON)
option(PROFILING "build with profiling" OFF)
option(DEBUG_BUILD "build a debug build (NOT relevant on iOS/Windows)" ON)

# Generic compilation options
option(WARNINGS "allow warnings" OFF)
option(OPTIMIZE "optimize all code" ON)
option(OPTIMIZE_SIZE "optimize all code for smallest size" OFF)

# Enable jemalloc/tcmalloc if available?
option(OTHER_MALLOC "use jemalloc or tcmalloc if present" ON)

# Enable LTO?
option(LTO "enable link-time optimization" OFF)

include(build-etc/compiler.cmake)

# Use jemalloc?
if (OTHER_MALLOC AND MALLOC)
    set (CMAKE_C_STANDARD_LIBRARIES "${CMAKE_CXX_STANDARD_LIBRARIES} ${MALLOC}")
endif ()

include_directories(include)
add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(bench)

if (DOXYGEN_FOUND)
    doxygen_add_docs(doxygen src include ALL)
endif ()
