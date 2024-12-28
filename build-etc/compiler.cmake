if(compiler_included)
  return()
endif()

set(compiler_included true)

add_library(cmake_base_compiler_options INTERFACE)
add_library(with_asan INTERFACE)
add_library(with_ubsan INTERFACE)
add_library(with_tsan INTERFACE)
add_library(with_msan INTERFACE)

if (UNIX AND NOT IOS AND NOT ANDROID)
    target_compile_options(with_asan INTERFACE -fsanitize=address -fno-omit-frame-pointer)
    target_link_libraries(with_asan INTERFACE -fsanitize=address)

    target_compile_options(with_ubsan INTERFACE -fsanitize=undefined -fno-omit-frame-pointer)
    target_link_libraries(with_ubsan INTERFACE -fsanitize=undefined)

    target_compile_options(with_tsan INTERFACE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_libraries(with_tsan INTERFACE -fsanitize=thread)

    target_compile_options(with_msan INTERFACE -fsanitize=memory -fno-omit-frame-pointer)
    target_link_libraries(with_msan INTERFACE -fsanitize=memory)

    if (OPTIMIZE)
        if (OPTIMIZE_SIZE)
            target_compile_options(cmake_base_compiler_options INTERFACE -Os)
            target_link_libraries(cmake_base_compiler_options INTERFACE -Os)
        else ()
            target_compile_options(cmake_base_compiler_options INTERFACE -O2)
            target_link_libraries(cmake_base_compiler_options INTERFACE -O2)
        endif ()
    else ()
        target_compile_options(cmake_base_compiler_options INTERFACE -O0)
        target_link_libraries(cmake_base_compiler_options INTERFACE -O0)
    endif ()

    if (DEBUG_SYMBOLS)
        target_compile_options(cmake_base_compiler_options INTERFACE -g3 -ggdb -gdwarf-2)
        target_link_libraries(cmake_base_compiler_options INTERFACE -g3 -ggdb -gdwarf-2)
    endif ()

    if (PROFILING)
        target_compile_options(cmake_base_compiler_options INTERFACE -pg)
        target_link_libraries(cmake_base_compiler_options INTERFACE -pg)
    endif ()

    if (DEBUG_BUILD)
        # Useful things in a debug build that may be slow in a release build...
        add_definitions(-DDEBUG -D_DEBUG)
        target_compile_definitions(cmake_base_compiler_options INTERFACE -D_FORTIFY_SOURCE=2)
        target_compile_options(cmake_base_compiler_options INTERFACE -fstack-protector)
    endif ()

    # ensure we still do some basic cleanup even without optimization
    target_compile_options(cmake_base_compiler_options INTERFACE -ffunction-sections -fdata-sections)
    if (APPLE)
        target_link_libraries(cmake_base_compiler_options INTERFACE -dead_strip)
    else ()
        target_link_libraries(cmake_base_compiler_options INTERFACE -Wl,--gc-sections)
    endif ()

    # Avoid some common linker mistakes.
    if (NOT APPLE)
        target_link_libraries(cmake_base_compiler_options INTERFACE -Wl,-z,defs -Wl,-z,now -Wl,-z,relro)
    endif ()

    if (COVERAGE)
        target_compile_options(cmake_base_compiler_options INTERFACE -coverage)
        target_link_libraries(cmake_base_compiler_options INTERFACE -coverage)
    endif ()
elseif (MSVC)
    # Add some common definitions - e.g. don't put min() and max() in the
    # global namespace.
    add_definitions(-D_HAS_EXCEPTIONS=0 -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)

    # Disable RTTI.
    string(REGEX REPLACE "/GR" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
endif ()

add_library(cmake_warning_options INTERFACE)
if (UNIX AND NOT IOS AND NOT ANDROID)
  target_compile_definitions(cmake_warning_options
                             INTERFACE -D_XOPEN_SOURCE=600
                                       -D_POSIX_C_SOURCE=200809L
                                       -D_DEFAULT_SOURCE=1
                                       $<$<BOOL:APPLE>:_DARWIN_C_SOURCE=200809L>)
  target_compile_options(cmake_warning_options
                         INTERFACE -Wall
                                   -Wextra
                                   -Wshadow
                                   $<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>
                                   $<$<COMPILE_LANGUAGE:CXX>:-Wno-old-style-cast>
                                   -Wcast-align
                                   -Wunused
                                   $<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>
                                   -Wpedantic
                                   -Wconversion
                                   -Wsign-conversion
                                   -Wnull-dereference
                                   -Wdouble-promotion
                                   -Wformat=2
                                   $<$<C_COMPILER_ID:GNU>:-Wmisleading-indentation>
                                   $<$<C_COMPILER_ID:GNU>:-Wduplicated-cond>
                                   $<$<C_COMPILER_ID:GNU>:-Wduplicated-branches>
                                   $<$<C_COMPILER_ID:GNU>:-Wlogical-op>
                                   $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wuseless-cast>
                                   -Wstack-protector
                        )
endif ()

if (MSVC)
    # Be pretty verbose about warnings in debug builds
    set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /W3 /WX")
    set (CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /WX")
endif ()

# Set up our warning flags.
if (UNIX)
    if (NOT WARNINGS)
        target_compile_options(cmake_warning_options INTERFACE -Werror)
    endif ()
endif ()


add_library(haven_compiler_options INTERFACE)

# Some targets (e.g. those depending on gtest) are not warning-clean with the above set of warnings.
add_library(haven_compiler_options_no_warnings INTERFACE)

target_link_libraries(haven_compiler_options INTERFACE haven_compiler_options_no_warnings cmake_warning_options)
target_link_libraries(haven_compiler_options_no_warnings INTERFACE cmake_base_compiler_options)

if (ASAN)
    target_link_libraries(haven_compiler_options INTERFACE with_asan)
endif ()

if (UBSAN)
    target_link_libraries(haven_compiler_options INTERFACE with_ubsan)
endif ()

if (TSAN)
    target_link_libraries(haven_compiler_options INTERFACE with_tsan)
endif ()

if (MSAN)
    target_link_libraries(haven_compiler_options INTERFACE with_msan)
endif ()
