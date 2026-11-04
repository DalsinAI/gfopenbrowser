# GFOpenBrowser's WebCore programs, built inside the WebKit build tree
# (Source/PlatformAmiga.cmake adds this folder when OPENBROWSER_WEBCORE_DIR
# names it), so they share WebKit's flags and link its static libraries.
# MIT, Copyright (c) 2026 AmiCradle Team.

set(OB_THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/openmail")

# The page layer: WebCore's clients, the view and its C interface, built
# once as the static library OBCore and linked into each program. A change
# here compiles that file and relinks; it never compiles WebCore.
set(OB_CORE_SOURCES
    AmigaBackForwardList.cpp
    AmigaChromeClient.cpp
    AmigaCookieJar.cpp
    AmigaDownload.cpp
    AmigaEditorClient.cpp
    AmigaFrameLoaderClient.cpp
    AmigaHttpCache.cpp
    AmigaLoaderStrategy.cpp
    AmigaNetworkSetup.c
    AmigaPlatformStrategies.cpp
    ob_jsoptions.c
    AmigaSessionStorage.cpp
    AmigaWebView.cpp
    ob_httpcache.c
    ob_tata.c
    ob_profile.cpp
    ob_webview.cpp
    tata/tata_ads.c
)
# Each program compiles these itself: they define symbols (library bases,
# the stack size) that the C library also has, and an object in a program
# always wins over one in a library.
set(OB_PROGRAM_SOURCES
    AmigaGlobals.c
    ${OB_THIRD_PARTY}/oam_stack.c
)

# ICU's data (OB_ICU_DATA): "file" (the default) reads ICU's common data file
# at start from LOCALE:Unicode/ or PROGDIR:Unicode/ (ob_icudata.c), and each
# program carries an empty data block (ob_icustub.c), which keeps
# libicudata.a's 5 MB of tables out of it; "linked" links the tables in as
# before; "library" takes ICU's code and data from unicode.library: the
# toolchain's OB_ICU_DIR names scripts/build-unicode-library.sh's folder,
# whose libicuuc.a sends each ICU call to the library, and ob_icudata.c
# opens it at start in place of reading the file.
set(OB_ICU_DATA "file" CACHE STRING "ICU data: file (read at start), linked, or library (unicode.library)")
if (OB_ICU_DATA STREQUAL "file")
    list(APPEND OB_CORE_SOURCES ob_icudata.c)
    list(APPEND OB_PROGRAM_SOURCES ob_icustub.c)
    add_compile_definitions(OB_ICU_DATA_FILE=1)
elseif (OB_ICU_DATA STREQUAL "library")
    # OB_UNICODE_DIR: that folder, for ul_client.h. A folder configured
    # before keeps its ICU headers (the same 78.3 ones) and is pointed at
    # the folder's libraries through FindICU's cache entries instead
    # (src/unicodelib/README.md), so the switch only relinks.
    if (DEFINED ENV{OB_ICU_DIR})
        set(OB_UNICODE_DIR_DEFAULT "$ENV{OB_ICU_DIR}")
    else ()
        set(OB_UNICODE_DIR_DEFAULT "${AC_WEBKIT_DEPS}")
    endif ()
    set(OB_UNICODE_DIR "${OB_UNICODE_DIR_DEFAULT}" CACHE PATH "scripts/build-unicode-library.sh's ICU folder")
    if (NOT EXISTS "${OB_UNICODE_DIR}/include/ul_client.h")
        message(FATAL_ERROR "OB_ICU_DATA=library needs OB_UNICODE_DIR (or OB_ICU_DIR) set to scripts/build-unicode-library.sh's ICU folder")
    endif ()
    list(APPEND OB_CORE_SOURCES ob_icudata.c)
    add_compile_definitions(OB_ICU_DATA_FILE=1 OB_ICU_LIBRARY=1)
elseif (NOT OB_ICU_DATA STREQUAL "linked")
    message(FATAL_ERROR "OB_ICU_DATA must be file, linked or library")
endif ()

# gfopenservice.device and openweb.fetch (DalsinAI/gfopenservice, checked
# out beside this repository, or OPENSERVICE_DIR). Required: every load of
# the engine goes through openweb.fetch (WebKit patch 0075, no curl).
# Page pictures are also decoded by a board or a paired Cradle when one
# offers it (ob_media.c).
set(OB_OPENSERVICE_DIR "$ENV{OPENSERVICE_DIR}")
if (NOT EXISTS "${OB_OPENSERVICE_DIR}/include/devices/gfopenservice.h")
    set(OB_OPENSERVICE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../gfopenservice")
endif ()
if (NOT EXISTS "${OB_OPENSERVICE_DIR}/include/devices/gfopenservice.h" OR NOT EXISTS "${OB_OPENSERVICE_DIR}/src/webfetch.c")
    message(FATAL_ERROR "gfopenservice not found at ${OB_OPENSERVICE_DIR}: the engine's network is "
        "openweb.fetch (include/openweb/webfetch.h, src/webfetch.c, src/wfnet*.c). Check out "
        "DalsinAI/gfopenservice beside this repository, or set OPENSERVICE_DIR.")
endif ()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${OB_OPENSERVICE_DIR}/include/openweb/webfetch.h")
file(STRINGS "${OB_OPENSERVICE_DIR}/include/openweb/webfetch.h" OB_WEBFETCH_ASYNC REGEX "wf_async_start")
if (NOT OB_WEBFETCH_ASYNC)
    message(FATAL_ERROR "${OB_OPENSERVICE_DIR}/include/openweb/webfetch.h has no wf_async_start: "
        "gfopenservice is older than openweb.fetch protocol 2 (#14); update it.")
endif ()
list(APPEND OB_CORE_SOURCES ob_media.c)
set(OB_OPENSERVICE 1)

# The datatypes' stream class (DalsinAI/gfopendatatypes, checked out beside
# this repository, or OPENDATATYPES_DIR): the native transport undoes gzip,
# deflate and br through stream.datatype, gzip.datatype and brotli.datatype
# (gfopenservice's src/wfnet_dec.c), so wfnet_amiga.c needs
# datatypes/streamclass.h.
set(OB_OPENDATATYPES_DIR "$ENV{OPENDATATYPES_DIR}")
if (NOT EXISTS "${OB_OPENDATATYPES_DIR}/Datatypes/include/datatypes/streamclass.h")
    set(OB_OPENDATATYPES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../gfopendatatypes")
endif ()
if (NOT EXISTS "${OB_OPENDATATYPES_DIR}/Datatypes/include/datatypes/streamclass.h")
    message(FATAL_ERROR "gfopendatatypes not found at ${OB_OPENDATATYPES_DIR}: openweb.fetch's native "
        "transport needs Datatypes/include/datatypes/streamclass.h. Check out DalsinAI/gfopendatatypes "
        "beside this repository, or set OPENDATATYPES_DIR.")
endif ()

# openweb.fetch's client and its native transport (gfopenservice's
# src/webfetch.c and src/wfnet*.c), WebCore's network backend. WebCore's
# OpenWebFetchAmiga.c is given the client's headers. The native transport
# reaches gfopentls.library through GFOpenTLS's headers and the datatypes
# through gfopendatatypes' (wfnet_amiga.c and wfnet_dec.c only).
file(GLOB OB_WFNET_SOURCES "${OB_OPENSERVICE_DIR}/src/wfnet*.c")
list(APPEND OB_CORE_SOURCES "${OB_OPENSERVICE_DIR}/src/webfetch.c" ${OB_WFNET_SOURCES})
set_property(SOURCE "${OB_OPENSERVICE_DIR}/src/wfnet_amiga.c" APPEND PROPERTY
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/gfopentls/include")
set_property(SOURCE "${OB_OPENSERVICE_DIR}/src/wfnet_amiga.c" "${OB_OPENSERVICE_DIR}/src/wfnet_dec.c" APPEND PROPERTY
    INCLUDE_DIRECTORIES "${OB_OPENDATATYPES_DIR}/Datatypes/include")
set_property(SOURCE "${WEBCORE_DIR}/platform/network/amiga/OpenWebFetchAmiga.c" TARGET_DIRECTORY WebCore APPEND PROPERTY
    INCLUDE_DIRECTORIES "${OB_OPENSERVICE_DIR}/include")

# gfopengpu.library's stream and inline calls (DalsinAI/gfopenrtg, checked
# out beside this repository, or OPENAMIGARTG_DIR): the window is painted
# through GFOpenGPU (WebKit patch 0076), which the engine opens at run time.
# Headers only: nothing is linked.
set(OB_OPENAMIGARTG_DIR "$ENV{OPENAMIGARTG_DIR}")
if (NOT EXISTS "${OB_OPENAMIGARTG_DIR}/include/gfopengpu/stream.h")
    set(OB_OPENAMIGARTG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../gfopenrtg")
endif ()
if (NOT EXISTS "${OB_OPENAMIGARTG_DIR}/include/gfopengpu/stream.h" OR NOT EXISTS "${OB_OPENAMIGARTG_DIR}/include/inline/gfopengpu.h")
    message(FATAL_ERROR "gfopenrtg not found at ${OB_OPENAMIGARTG_DIR}: the engine paints through "
        "gfopengpu.library (include/gfopengpu/stream.h, include/inline/gfopengpu.h). Check out "
        "DalsinAI/gfopenrtg beside this repository, or set OPENAMIGARTG_DIR.")
endif ()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${OB_OPENAMIGARTG_DIR}/include/gfopengpu/stream.h")
set_property(SOURCE "${WEBCORE_DIR}/platform/graphics/amiga/OpenGPUPaintAmiga.c"
    "${WEBCORE_DIR}/platform/graphics/amiga/OpenGPULibraryAmiga.c"
    "${WEBCORE_DIR}/platform/graphics/amiga/OpenGPUGlyphsAmiga.c" TARGET_DIRECTORY WebCore APPEND PROPERTY
    INCLUDE_DIRECTORIES "${OB_OPENAMIGARTG_DIR}/include")

# gfopenlayout.library's UI calls (DalsinAI/gfopenlayout, checked out
# beside this repository, or GFOPENLAYOUT_DIR): the window's dialogs, find
# bar, downloads and preferences (ob_dialogs.c), which the window opens at
# run time (version 11). Headers only: nothing is linked.
set(OB_GFOPENLAYOUT_DIR "$ENV{GFOPENLAYOUT_DIR}")
if (NOT EXISTS "${OB_GFOPENLAYOUT_DIR}/include/inline/gfopenlayout.h")
    set(OB_GFOPENLAYOUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../gfopenlayout")
endif ()
if (NOT EXISTS "${OB_GFOPENLAYOUT_DIR}/include/inline/gfopenlayout.h" OR NOT EXISTS "${OB_GFOPENLAYOUT_DIR}/include/gfopenlayout_ui_amiga.h")
    message(FATAL_ERROR "gfopenlayout not found at ${OB_GFOPENLAYOUT_DIR}: the window's dialogs are drawn by "
        "gfopenlayout.library 11 (include/inline/gfopenlayout.h, include/gfopenlayout_ui_amiga.h). Check out "
        "DalsinAI/gfopenlayout beside this repository, or set GFOPENLAYOUT_DIR.")
endif ()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${OB_GFOPENLAYOUT_DIR}/include/inline/gfopenlayout.h")
set_property(SOURCE ob_dialogs.c APPEND PROPERTY INCLUDE_DIRECTORIES "${OB_GFOPENLAYOUT_DIR}/include")

set(OB_FRAMEWORKS
    JavaScriptCore
    PAL
    WTF
    WebCore
    bmalloc
)
set(OB_PRIVATE_INCLUDE_DIRECTORIES
    ${CMAKE_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OB_THIRD_PARTY}
    # AmigaNetwork.h: the Amiga network layer's hooks (not a public header)
    ${WEBCORE_DIR}/platform/network/amiga
)
list(APPEND OB_PRIVATE_INCLUDE_DIRECTORIES ${OB_OPENSERVICE_DIR}/include)
if (OB_ICU_DATA STREQUAL "library")
    list(APPEND OB_PRIVATE_INCLUDE_DIRECTORIES ${OB_UNICODE_DIR}/include)
endif ()
set(OB_SYSTEM_INCLUDE_DIRECTORIES
    ${AC_WEBCORE_DEPS}/include
    ${AC_WEBCORE_DEPS}/include/cairo
    ${AC_WEBCORE_DEPS}/include/pixman-1
)

# Static archives that depend on each other (WebCore, JavaScriptCore, WTF,
# cairo, pixman...) are linked as one group, so their order does not
# matter, followed by what libnix programs need. No curl or nghttp2 (WebKit
# patch 0075): openweb.fetch's native transport opens bsdsocket.library,
# gfopentls.library and datatypes.library itself, so there is no network or
# TLS library to link.
macro(OB_LINK _target)
    # OB_ICU_DATA=library: icui18n, icuuc and icudata are the library's stubs
    if (OB_ICU_DATA STREQUAL "library")
        target_link_directories(${_target} PRIVATE ${OB_UNICODE_DIR}/lib)
    endif ()
    target_link_directories(${_target} PRIVATE ${AC_WEBCORE_DEPS}/lib ${AC_WEBKIT_DEPS}/lib)
    # A link map beside each program (bin/NAME.map): which object every
    # address belongs to, static functions included, which the program's
    # own symbol table leaves out. For profiles, and for where the size goes.
    target_link_options(${_target} PRIVATE -Wl,-Map=${CMAKE_BINARY_DIR}/bin/${_target}.map)
    target_link_libraries(${_target} PRIVATE
        -Wl,--start-group
        OBCore WebCore PAL JavaScriptCore WTF bmalloc
        cairo pixman-1 png z oxp sqlite3
        icui18n icuuc icudata
        amiga pthread atomic stdc++ m
        -Wl,--end-group
    )
endmacro()

set(OBCore_SOURCES ${OB_CORE_SOURCES})
set(OBCore_LIBRARY_TYPE STATIC)
set(OBCore_FRAMEWORKS ${OB_FRAMEWORKS})
set(OBCore_PRIVATE_INCLUDE_DIRECTORIES ${OB_PRIVATE_INCLUDE_DIRECTORIES})
set(OBCore_SYSTEM_INCLUDE_DIRECTORIES ${OB_SYSTEM_INCLUDE_DIRECTORIES})
# Compile against WebKit as the programs do: linking the frameworks brings
# their include folders and definitions. WebCore's generated and private
# headers must exist before OBCore compiles.
set(OBCore_PRIVATE_LIBRARIES WebKit::WebCore WebKit::PAL WebKit::JavaScriptCore WebKit::WTF WebKit::bmalloc)
set(OBCore_DEPENDENCIES WebCore)
WEBKIT_LIBRARY_DECLARE(OBCore)
WEBKIT_LIBRARY(OBCore)
if (OB_OPENSERVICE)
    target_compile_definitions(OBCore PRIVATE OB_OPENSERVICE=1)
endif ()

# obcore-dump: load a local HTML file, print its render tree (milestone 2a)
# and paint it into a PNG (2b).
set(obcore-dump_SOURCES
    ${OB_PROGRAM_SOURCES}
    DumpMain.cpp
)
set(obcore-dump_FRAMEWORKS ${OB_FRAMEWORKS})
set(obcore-dump_PRIVATE_INCLUDE_DIRECTORIES ${OB_PRIVATE_INCLUDE_DIRECTORIES})
set(obcore-dump_SYSTEM_INCLUDE_DIRECTORIES ${OB_SYSTEM_INCLUDE_DIRECTORIES})
WEBKIT_EXECUTABLE_DECLARE(obcore-dump)
WEBKIT_EXECUTABLE(obcore-dump)
OB_LINK(obcore-dump)

# obcore-view: the page layer without a window, through its C interface.
set(obcore-view_SOURCES
    ${OB_PROGRAM_SOURCES}
    obcore_view.c
)
set(obcore-view_FRAMEWORKS ${OB_FRAMEWORKS})
set(obcore-view_PRIVATE_INCLUDE_DIRECTORIES ${OB_PRIVATE_INCLUDE_DIRECTORIES})
set(obcore-view_SYSTEM_INCLUDE_DIRECTORIES ${OB_SYSTEM_INCLUDE_DIRECTORIES})
WEBKIT_EXECUTABLE_DECLARE(obcore-view)
WEBKIT_EXECUTABLE(obcore-view)
OB_LINK(obcore-view)

# WebBrowserEngine: the one engine behind webbrowser.library
# (src/webbrowser), serving every program's views through a public port.
set(WebBrowserEngine_SOURCES
    ${OB_PROGRAM_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../webbrowser/wb_engine.c
    ob_blit.c
    ob_browser_engine.c
    ob_dialogs.c
    ob_splash.c
)
set(WebBrowserEngine_FRAMEWORKS ${OB_FRAMEWORKS})
set(WebBrowserEngine_PRIVATE_INCLUDE_DIRECTORIES ${OB_PRIVATE_INCLUDE_DIRECTORIES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../webbrowser ${CMAKE_CURRENT_SOURCE_DIR}/../webbrowser/include)
set(WebBrowserEngine_SYSTEM_INCLUDE_DIRECTORIES ${OB_SYSTEM_INCLUDE_DIRECTORIES})
WEBKIT_EXECUTABLE_DECLARE(WebBrowserEngine)
WEBKIT_EXECUTABLE(WebBrowserEngine)
OB_LINK(WebBrowserEngine)

# GFOpenBrowser.engine: the browser window (GadTools) around the page layer, as
# a program of its own that runs without webbrowser.library. The package
# does not carry it: WebBrowserEngine above is the one engine there, with the
# same window (ob_browser_engine.c). Kept for tests that run the window
# directly (scripts/build-webcore.sh build-window-engine).
set(OpenBrowser_SOURCES
    ${OB_PROGRAM_SOURCES}
    ob_blit.c
    ob_browser.c
    ob_dialogs.c
    ob_splash.c
)
set(OpenBrowser_OUTPUT_NAME GFOpenBrowser.engine)
set(OpenBrowser_FRAMEWORKS ${OB_FRAMEWORKS})
set(OpenBrowser_PRIVATE_INCLUDE_DIRECTORIES ${OB_PRIVATE_INCLUDE_DIRECTORIES})
set(OpenBrowser_SYSTEM_INCLUDE_DIRECTORIES ${OB_SYSTEM_INCLUDE_DIRECTORIES})
WEBKIT_EXECUTABLE_DECLARE(OpenBrowser)
WEBKIT_EXECUTABLE(OpenBrowser)
OB_LINK(OpenBrowser)

# JavaScriptEngine: the program behind javascript.library (src/jslib),
# built here so that its JavaScriptCore is the browser's own: the same
# patches, flags (OB_OPTIMIZE, OB_PCREL, OB_GC_SECTIONS) and archives, and
# folded like the browser's programs. Its sources compile as WebKit's jsc
# shell does, with JavaScriptCore's own include folders (jsl_cache.cpp is
# on JavaScriptCore's private interface). It links no WebCore.
set(JavaScriptEngine_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../jslib/jsl_cache.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../jslib/jsl_core.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../jslib/jsl_engine.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../jslib/jsl_os_exec.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../jslib/jsimage_host.c
)
if (OB_ICU_DATA STREQUAL "file")
    list(APPEND JavaScriptEngine_SOURCES ob_icudata.c ob_icustub.c)
elseif (OB_ICU_DATA STREQUAL "library")
    list(APPEND JavaScriptEngine_SOURCES ob_icudata.c)
endif ()
set(JavaScriptEngine_FRAMEWORKS JavaScriptCore WTF bmalloc)
set(JavaScriptEngine_PRIVATE_INCLUDE_DIRECTORIES
    $<TARGET_PROPERTY:JavaScriptCore,INCLUDE_DIRECTORIES>
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../jslib
    ${CMAKE_CURRENT_SOURCE_DIR}/../jslib/sdk/Include_H
)
if (OB_ICU_DATA STREQUAL "library")
    list(APPEND JavaScriptEngine_PRIVATE_INCLUDE_DIRECTORIES ${OB_UNICODE_DIR}/include)
endif ()
WEBKIT_EXECUTABLE_DECLARE(JavaScriptEngine)
WEBKIT_EXECUTABLE(JavaScriptEngine)
if (OB_ICU_DATA STREQUAL "library")
    target_link_directories(JavaScriptEngine PRIVATE ${OB_UNICODE_DIR}/lib)
endif ()
target_link_directories(JavaScriptEngine PRIVATE ${AC_WEBKIT_DEPS}/lib)
target_link_options(JavaScriptEngine PRIVATE -Wl,-Map=${CMAKE_BINARY_DIR}/bin/JavaScriptEngine.map)
# Nothing in the engine reads jsimage_id or jsimage_client (jsimage.py
# stamps the first, the client sets the second), so OB_GC_SECTIONS would
# drop them; -u keeps both.
target_link_options(JavaScriptEngine PRIVATE -Wl,-u,_jsimage_id -Wl,-u,_jsimage_client)
# As a jsimage (scripts/jsimage.py, "build-webcore.sh jsimage"), the engine
# keeps every name of JavaScriptCore, WTF and bmalloc that its clients
# linked: jsimage/needs.ld, an EXTERN list (empty until then).
set(OB_JSIMAGE_NEEDS ${CMAKE_BINARY_DIR}/jsimage/needs.ld)
if (NOT EXISTS ${OB_JSIMAGE_NEEDS})
    file(WRITE ${OB_JSIMAGE_NEEDS} "/* jsimage: no clients yet */\n")
endif ()
target_link_options(JavaScriptEngine PRIVATE ${OB_JSIMAGE_NEEDS})
set_property(TARGET JavaScriptEngine APPEND PROPERTY LINK_DEPENDS ${OB_JSIMAGE_NEEDS})
target_link_libraries(JavaScriptEngine PRIVATE
    -Wl,--start-group
    JavaScriptCore WTF bmalloc
    icui18n icuuc icudata
    amiga pthread atomic stdc++ m
    -Wl,--end-group
)

# A jsimage client's first code (src/jslib/jsimage_client.c): it runs
# before the C library is set up, so it is built without it.
add_library(JSImageClient OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/../jslib/jsimage_client.c)
target_compile_options(JSImageClient PRIVATE -ffreestanding -fno-builtin)

# GFOpenBrowser: the small program the icon starts. It shows a title window at
# once and asks webbrowser.library (LIBS:, or the drawer's Libs before
# Install) for a browser window, which WebBrowserEngine opens. It needs nothing of WebKit's, only the C library,
# the stove's compat library (the toolchain wraps malloc) and libpthread
# (the toolchain links with -fexceptions, whose frame tables use it).
add_executable(OpenBrowserLauncher ob_launcher.c ob_splash.c)
target_include_directories(OpenBrowserLauncher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../webbrowser/include)
set_target_properties(OpenBrowserLauncher PROPERTIES OUTPUT_NAME GFOpenBrowser)
target_link_libraries(OpenBrowserLauncher PRIVATE -L${AC_OS32_STOVE}/compat -los32compat pthread)
