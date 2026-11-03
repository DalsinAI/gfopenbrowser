# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
#
# WebKit JSCOnly cross toolchain for AmigaOS 3.2.x with the os32-gcc16 stove
# (bebbo amiga-gcc, gcc branch amiga16.2, libnix, libpthread).
# Environment: OS32_GCC16 = the stove root (holds prefix/ and compat/),
#              OAB_DEPS   = the folder holding icu78-m68k-amigaos/ and, for
#                           WebCore, m68k-amigaos/ (scripts/build-webcore-deps.sh).
if (DEFINED ENV{OS32_GCC16})
    set(AC_OS32_STOVE "$ENV{OS32_GCC16}")
else ()
    set(AC_OS32_STOVE "$ENV{HOME}/AmiCradle/stoves/os32-gcc16")
endif ()
if (DEFINED ENV{OAB_DEPS})
    set(AC_OAB_DEPS "$ENV{OAB_DEPS}")
else ()
    set(AC_OAB_DEPS "$ENV{HOME}/openbrowser-deps")
endif ()
set(CMAKE_SYSTEM_NAME AmigaOS)
set(CMAKE_SYSTEM_PROCESSOR m68k)

set(AC_OS32_GCC16 "${AC_OS32_STOVE}/prefix")
set(CMAKE_C_COMPILER "${AC_OS32_GCC16}/bin/m68k-amigaos-gcc")
set(CMAKE_CXX_COMPILER "${AC_OS32_GCC16}/bin/m68k-amigaos-g++")
set(CMAKE_AR "${AC_OS32_GCC16}/bin/m68k-amigaos-ar" CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB "${AC_OS32_GCC16}/bin/m68k-amigaos-ranlib" CACHE FILEPATH "" FORCE)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# libpthread (Szilard Biro's, from amiga-gcc's aros-stuff) is a separate library.
set(CMAKE_HAVE_LIBC_PTHREAD FALSE CACHE INTERNAL "" FORCE)
set(CMAKE_HAVE_PTHREADS_CREATE FALSE CACHE INTERNAL "" FORCE)
set(CMAKE_HAVE_PTHREAD_CREATE TRUE CACHE INTERNAL "" FORCE)
set(CMAKE_THREAD_LIBS_INIT "-lpthread" CACHE STRING "" FORCE)

# OS32_CPU_FLAGS in the environment overrides the CPU flags. Builds are for
# the 68040 only (the Team's decision, 17 October 2026).
if (DEFINED ENV{OS32_CPU_FLAGS})
    set(AC_OS32_CPU_FLAGS "$ENV{OS32_CPU_FLAGS}")
else ()
    set(AC_OS32_CPU_FLAGS "-m68040 -m68881 -mcrt=nix20")
endif ()
# WebKit builds with -std=c++23 (strict), which makes newlib hide POSIX and BSD
# calls; _DEFAULT_SOURCE shows them again. nanosleep() and siginfo_t also need
# these POSIX options
# are announced; libnix/libpthread provide what WTF links against.
set(AC_OS32_POSIX_FLAGS "-D_DEFAULT_SOURCE=1 -D_POSIX_TIMERS=1 -D_POSIX_REALTIME_SIGNALS=1")
# On an Amiga address 0 is memory (exec's pointer is at 4). Without this flag
# GCC treats a pointer it can prove null as unreachable and puts TRAP #7
# (Software Failure 80000027) in place of the access; with it, a read of
# address 0 stays a read, as with older compilers.
set(AC_OS32_NULL_FLAGS "-fno-delete-null-pointer-checks")
# -mcas (GFOpenAmigaGCC's patch 0015): 1-, 2- and 4-byte atomics as CAS
# inline instead of libatomic calls under Forbid()/Permit(). It is safe here
# because the browser's heap and data are in Fast RAM; Chip RAM is why the
# compiler leaves it off by default. It is used when the stove's compiler
# has it; OB_CAS=0 leaves it out. 8-byte atomics stay calls, to
# src/webcore/ob_atomic.c's CAS2.
set(AC_OS32_CAS_FLAGS "")
if (NOT "$ENV{OB_CAS}" STREQUAL "0")
    execute_process(COMMAND "${CMAKE_C_COMPILER}" -m68040 -mcas -x c -E -
        INPUT_FILE /dev/null OUTPUT_QUIET ERROR_QUIET RESULT_VARIABLE AC_OS32_CAS_RESULT)
    if (AC_OS32_CAS_RESULT EQUAL 0)
        set(AC_OS32_CAS_FLAGS "-mcas")
    endif ()
endif ()
set(CMAKE_C_FLAGS_INIT "${AC_OS32_CPU_FLAGS} ${AC_OS32_CAS_FLAGS} ${AC_OS32_NULL_FLAGS} ${AC_OS32_POSIX_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${AC_OS32_CPU_FLAGS} ${AC_OS32_CAS_FLAGS} ${AC_OS32_NULL_FLAGS} ${AC_OS32_POSIX_FLAGS}")
# libnix has no aligned_alloc(); the stove's compat library supplies one with
# malloc/free wrapped so free() accepts aligned blocks (os32compat_malloc.c).
set(AC_OS32_WRAP_FLAGS "-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free -Wl,-u,___wrap_malloc -Wl,-u,___wrap_free -Wl,-u,___wrap_calloc -Wl,-u,___wrap_realloc")
# WebKit compiles with -fno-exceptions; on amigaos g++ then links a glue
# operator new (new_op.o) that clashes with libstdc++'s. A final -fexceptions
# on the link line keeps libstdc++'s own.
set(CMAKE_EXE_LINKER_FLAGS_INIT "${AC_OS32_CPU_FLAGS} -fexceptions ${AC_OS32_WRAP_FLAGS}")
# FindICU puts libicudata before libicuuc; list it again so its data symbol resolves.
# ICU: the static libraries scripts/build-icu.sh makes, or with OB_ICU_DIR
# the folder scripts/build-unicode-library.sh makes, whose libraries send
# every ICU call to unicode.library (the same headers, no ICU code or data).
if (DEFINED ENV{OB_ICU_DIR})
    set(AC_WEBKIT_DEPS "$ENV{OB_ICU_DIR}")
else ()
    set(AC_WEBKIT_DEPS "${AC_OAB_DEPS}/icu78-m68k-amigaos")
endif ()
set(CMAKE_CXX_STANDARD_LIBRARIES_INIT "${AC_WEBKIT_DEPS}/lib/libicudata.a -lpthread -latomic -lstdc++ -L${AC_OS32_STOVE}/compat -los32compat")

set(AC_WEBCORE_DEPS "${AC_OAB_DEPS}/m68k-amigaos")
set(CMAKE_FIND_ROOT_PATH "${AC_WEBKIT_DEPS};${AC_WEBCORE_DEPS};${AC_OS32_GCC16}/m68k-amigaos")
set(CMAKE_PREFIX_PATH "${AC_WEBKIT_DEPS};${AC_WEBCORE_DEPS}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(USE_SYSTEM_MALLOC ON CACHE BOOL "" FORCE)
set(ENABLE_JIT OFF CACHE BOOL "" FORCE)
set(ENABLE_DFG_JIT OFF CACHE BOOL "" FORCE)
set(ENABLE_FTL_JIT OFF CACHE BOOL "" FORCE)
set(ENABLE_WEBASSEMBLY OFF CACHE BOOL "" FORCE)
set(EVENT_LOOP_TYPE Generic CACHE STRING "" FORCE)
set(ENABLE_STATIC_JSC ON CACHE BOOL "" FORCE)
set(ENABLE_API_TESTS OFF CACHE BOOL "" FORCE)
