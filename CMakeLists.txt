cmake_minimum_required(VERSION 3.28)
project(haven C CXX ASM)

include("CMakeDependentOption")

include(build-etc/config.cmake)
include(build-etc/macros.cmake)

find_library(MALLOC NAMES jemalloc tcmalloc)

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)
find_package(Doxygen)

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
option(DEBUG_BUILD "build with extra debug options such as logging, even in a Release configuration (NOT relevant on iOS/Windows)" ON)

# Generic compilation options
option(WARNINGS "allow warnings" OFF)
option(OPTIMIZE "optimize all code" OFF)
option(OPTIMIZE_SIZE "optimize all code for smallest size" OFF)
option(COVERAGE "enable code coverage" OFF)

# Enable jemalloc/tcmalloc if available?
option(OTHER_MALLOC "use jemalloc or tcmalloc if present" ON)

# Enable LTO?
option(LTO "enable link-time optimization" OFF)

# Flags for compiling Haven code
set(HAVEN_BOOTSTRAP_COMPILE_FLAGS "-I /usr/include --O2" CACHE STRING "flags to use when compiling Haven code with the bootstrap compiler")
set(HAVEN_COMPILE_FLAGS "-I /usr/include --O2" CACHE STRING "flags to use when compiling Haven code with the main compiler")

include(build-etc/compiler.cmake)

# Use jemalloc?
if (OTHER_MALLOC AND MALLOC)
    set (CMAKE_C_STANDARD_LIBRARIES "${CMAKE_C_STANDARD_LIBRARIES} ${MALLOC}")
    set (CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_CXX_STANDARD_LIBRARIES} ${MALLOC}")
endif ()

include_directories(include src)

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(bench)

if (DOXYGEN_FOUND)
    doxygen_add_docs(doxygen src include)
endif ()

# Import third-party dependencies last, so they don't interfere with our settings
add_subdirectory(third_party)

include(CTest)
