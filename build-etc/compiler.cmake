set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED TRUE)

if (UNIX AND NOT IOS AND NOT ANDROID)
    if (ASAN)
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
        set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
        set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
    elseif (UBSAN)
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined")
        set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=undefined")
        set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=undefined")
    elseif (TSAN)
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread")
        set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=thread")
        set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
    elseif (MSAN)
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=memory")
        set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=memory")
        set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=memory")
    endif ()

    if (LTO)
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto")
        set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flto")
        set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto")

        # TODO(miselin): this is messy, but necessary for LTO to work on Ubuntu
        # Need to test with clang...
        if (CMAKE_COMPILER_IS_GNUCXX)
            set (CMAKE_RANLIB "gcc-ranlib")
            set (CMAKE_AR "gcc-ar")
        endif ()
    endif ()

    if (OPTIMIZE)
        if (OPTIMIZE_SIZE)
            set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Os")
            set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os")
            set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Os")
        else ()
            set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")
            set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2")
            set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -O2")
        endif ()
    else ()
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0")
        set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0")
        set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -O0")
    endif ()

    if (DEBUG_SYMBOLS)
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g3 -ggdb -gdwarf-2")
        set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g3 -ggdb -gdwarf-2")
        set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -g3 -ggdb -gdwarf-2")
    endif ()

    if (PROFILING)
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pg")
        set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pg")
        set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pg")
    endif ()

    if (DEBUG_BUILD)
        # Useful things in a debug build that may be slow in a release build...
        add_definitions(-DDEBUG -D_DEBUG)
        if (NOT (ASAN OR TSAN OR UBSAN))
            add_definitions(-D_FORTIFY_SOURCE=2)
        endif ()
    endif ()

    # ensure we still do some basic cleanup even without optimization
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffunction-sections -fdata-sections")
    if (APPLE)
        set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -dead_strip")
    else ()
        set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
    endif ()

    # Avoid some common linker mistakes.
    if (NOT APPLE)
        set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,defs -Wl,-z,now -Wl,-z,relro")
    endif ()
elseif (MSVC)
    # Add some common definitions - e.g. don't put min() and max() in the
    # global namespace.
    add_definitions(-D_HAS_EXCEPTIONS=0 -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)

    # Disable RTTI.
    string(REGEX REPLACE "/GR" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})

    # Are we using clang-cl? If so we need to fix some minor issues.
    if (USING_CLANG_CL)
        # Yes. We need to do some... magic... to get proper GNU++14 support.
        # And yes, we need to specify gnu++14, because that's what the VC++
        # headers are written for.
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Xclang -std=gnu++14")
    endif ()

    # Enable intrinsics but still use precise floating point for release builds.
    set (CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /fp:precise /Oi")
endif ()

if (UNIX AND NOT IOS AND NOT ANDROID)
    # -Wno-switch avoids warnings about unhandled switch cases.
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-switch -fdiagnostics-color=always -Werror=implicit-function-declaration")
endif ()

if (MSVC)
    # Be pretty verbose about warnings in debug builds
    set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /W3 /WX")
    set (CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /WX")
endif ()

# Set up our warning flags.
if (UNIX)
    if (NOT WARNINGS)
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
        set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror")
    endif ()
endif ()
