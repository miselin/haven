cmake_minimum_required(VERSION 3.28)
project(haven C CXX ASM)

cmake_policy(SET CMP0171 NEW)

include("CMakeDependentOption")
include(CheckCCompilerFlag)

include(build-etc/config.cmake)
include(build-etc/macros.cmake)

find_library(MALLOC NAMES jemalloc tcmalloc)

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)
find_package(GTest REQUIRED)
find_package(benchmark REQUIRED)
find_package(Doxygen)

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})
link_directories(${LLVM_LIBRARY_DIRS})

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

# C header path for Haven to use
if (APPLE)
    set(HAVEN_APPLE_CLANG_INCLUDE "")
    if (LLVM_TOOLS_BINARY_DIR)
        execute_process(
            COMMAND ${LLVM_TOOLS_BINARY_DIR}/clang -print-resource-dir
            OUTPUT_VARIABLE HAVEN_CLANG_RESOURCE_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if (HAVEN_CLANG_RESOURCE_DIR)
            set(HAVEN_APPLE_CLANG_INCLUDE "-I ${HAVEN_CLANG_RESOURCE_DIR}/include")
        endif ()
    endif ()

    if (CMAKE_OSX_SYSROOT)
        set(HAVEN_DEFAULT_C_FLAGS "-isysroot ${CMAKE_OSX_SYSROOT} ${HAVEN_APPLE_CLANG_INCLUDE}")
    else ()
        execute_process(
            COMMAND xcrun --show-sdk-path
            OUTPUT_VARIABLE HAVEN_SDK_PATH
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if (HAVEN_SDK_PATH)
            set(HAVEN_DEFAULT_C_FLAGS "-isysroot ${HAVEN_SDK_PATH} ${HAVEN_APPLE_CLANG_INCLUDE}")
        else ()
            set(HAVEN_DEFAULT_C_FLAGS "${HAVEN_APPLE_CLANG_INCLUDE}")
        endif ()
    endif ()
else ()
    set(HAVEN_DEFAULT_C_FLAGS "-I /usr/include")
endif ()
set(HAVEN_C_FLAGS "${HAVEN_DEFAULT_C_FLAGS}" CACHE STRING "C flags to pass to Haven (e.g. for header directories)")
string(REPLACE ";" " " HAVEN_C_FLAGS "${HAVEN_C_FLAGS}")

# Flags for compiling Haven code
set(HAVEN_BOOTSTRAP_COMPILE_FLAGS "${HAVEN_C_FLAGS} --O2" CACHE STRING "flags to use when compiling Haven code with the bootstrap compiler")
set(HAVEN_COMPILE_FLAGS "${HAVEN_C_FLAGS} --O2" CACHE STRING "flags to use when compiling Haven code with the main compiler")

include(build-etc/compiler.cmake)

find_program(DUNE_EXECUTABLE dune)
if (DUNE_EXECUTABLE)
    add_custom_target(haven_ml_cli
        COMMAND ${DUNE_EXECUTABLE} build bin/haven.exe
        BYPRODUCTS ${CMAKE_SOURCE_DIR}/src-ml/_build/default/bin/haven.exe
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src-ml
        COMMENT "Building OCaml Haven compiler")
endif ()

# Use jemalloc?
if (OTHER_MALLOC AND MALLOC)
    set (CMAKE_C_STANDARD_LIBRARIES "${CMAKE_C_STANDARD_LIBRARIES} ${MALLOC}")
    set (CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_CXX_STANDARD_LIBRARIES} ${MALLOC}")
endif ()

include_directories(include src)

enable_testing()

add_subdirectory(src)
add_subdirectory(src-new)
add_subdirectory(tests)
add_subdirectory(bench)

if (DOXYGEN_FOUND)
    doxygen_add_docs(doxygen src include)
endif ()

include(CTest)

install(TARGETS haven havenfmt runtime
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        INCLUDES DESTINATION include)
