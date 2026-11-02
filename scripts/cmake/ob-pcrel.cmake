# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
#
# OB_PCREL=1: the files our GCC cannot build pc-relative are built with
# absolute addresses (-mno-pcrel -fno-PIC) instead. Such code links into
# the one-hunk program as before, with HUNK_RELOC32 relocations.
#
# - JavaScriptCore's bytecode/CodeBlock.cpp at -O2: GCC 16.2.0b stops with
#   "internal compiler error: in change_address_1, at emit-rtl.cc:2315"
#   (RTL pass final) with -mpcrel, with -fPIC or -fpic. The unified bundle
#   that includes it gets the options, so the whole bundle (about 165 KB of
#   code) stays absolute.
#
# Read through CMAKE_PROJECT_WebKit_INCLUDE; the targets exist only at the
# end of the top-level folder, hence the deferred call.
get_property(_ob_pcrel_done GLOBAL PROPERTY OB_PCREL_DEFERRED)
if (NOT _ob_pcrel_done)
    set_property(GLOBAL PROPERTY OB_PCREL_DEFERRED TRUE)
    function(_ob_pcrel_absolute target file)
        if (NOT TARGET ${target})
            message(WARNING "OB_PCREL: no target ${target}")
            return()
        endif ()
        get_target_property(sources ${target} SOURCES)
        set(found "")
        foreach (source IN LISTS sources)
            if (source MATCHES "/unified-sources/UnifiedSource-[^/]*\\.cpp$" AND EXISTS "${source}")
                file(READ "${source}" bundle)
                string(FIND "${bundle}" "#include \"${file}\"" at)
                if (NOT at EQUAL -1)
                    list(APPEND found "${source}")
                endif ()
            elseif (source MATCHES "/${file}$")
                list(APPEND found "${source}")
            endif ()
        endforeach ()
        if (NOT found)
            message(WARNING "OB_PCREL: ${file} not found in ${target}")
            return()
        endif ()
        foreach (source IN LISTS found)
            set_property(SOURCE "${source}" TARGET_DIRECTORY ${target}
                APPEND PROPERTY COMPILE_OPTIONS -mno-pcrel -fno-PIC)
            message(STATUS "OB_PCREL: ${source} built with absolute addresses")
        endforeach ()
    endfunction()
    function(_ob_pcrel)
        _ob_pcrel_absolute(JavaScriptCore bytecode/CodeBlock.cpp)
    endfunction()
    cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _ob_pcrel)
endif ()
