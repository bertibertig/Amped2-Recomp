cmake_minimum_required(VERSION 3.20)

# ============================================================================
# Amped 2 (Xbox) - Static Recompilation
#
# Builds on Windows (MSVC or MinGW: D3D11/XInput/XAudio2) and on Linux/macOS
# (GCC/Clang: SDL2 + OpenGL 3.3 via libepoxy). The runtime picks its backend
# per platform; this file only adds the host-specific link libraries.
# ============================================================================

project(amped2_recomp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Common definitions
add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# ── xboxrecomp toolkit ──────────────────────────────────────────
#
# Option A: xboxrecomp as a subdirectory (simplest)
#   Place xboxrecomp/ next to your game project and use a relative path.
#
# Option B: find_package (if you install xboxrecomp system-wide)
#   find_package(xboxrecomp REQUIRED)
#
# Adjust this path to point to your xboxrecomp clone:
set(XBOXRECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/xboxrecomp" CACHE PATH
    "Path to the xboxrecomp toolkit root directory")

# Add xboxrecomp as a subdirectory -- this builds all 6 runtime libraries
# and provides the 'xboxrecomp' umbrella target to link against.
add_subdirectory(${XBOXRECOMP_DIR} ${CMAKE_BINARY_DIR}/xboxrecomp)

# Host game controllers: our src/input/host_pad.c replaces the runtime's
# xinput_device.c in its xbox_input library (same API, xinput_xbox.h), so the
# submodule stays unmodified. Adds hotplug, the binding table and the
# Black/White fix -- see the file's header. amped2_keys.c adds the game's own
# keys (Space = A, Escape = Start ...) to the runtime's keyboard map.
get_target_property(AMPED2_INPUT_SOURCES xbox_input SOURCES)
list(REMOVE_ITEM AMPED2_INPUT_SOURCES xinput_device.c)
set_property(TARGET xbox_input PROPERTY SOURCES
    ${AMPED2_INPUT_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/src/input/host_pad.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/input/amped2_keys.c)

# ── Recompiled game executable ──────────────────────────────────

# Collect generated recompiled code.
# The recomp pipeline outputs files into src/recomp/gen/.
# With --split 250, you get recomp_0000.c, recomp_0001.c, etc.
# CONFIGURE_DEPENDS so re-running the recompiler picks up added/removed files
# without a manual re-configure. A plain GLOB is evaluated once at configure
# time, so a newly generated file (an extra chunk, recomp_stubs_unresolved.c)
# is silently left out of the build and shows up as unresolved externals.
file(GLOB RECOMP_GEN_SOURCES CONFIGURE_DEPENDS "src/recomp/gen/*.c")

# Which Amped 2 build (pal / ntsc_u) the generated code comes from: written by
# scripts/run_pipeline.sh next to it. Selects src/version/<build>/ (the
# hand-written overrides and data addresses for that XBE). Without generated
# code (host-only build, CI) PAL is compiled.
set(AMPED2_XBE_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/recomp/gen/xbe_version.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${AMPED2_XBE_VERSION_FILE})
if(EXISTS ${AMPED2_XBE_VERSION_FILE})
    file(STRINGS ${AMPED2_XBE_VERSION_FILE} AMPED2_XBE_VERSION LIMIT_COUNT 1)
elseif(RECOMP_GEN_SOURCES)
    message(FATAL_ERROR "src/recomp/gen has no xbe_version.txt: re-run scripts/run_pipeline.sh recomp")
else()
    set(AMPED2_XBE_VERSION pal)
endif()
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/version/${AMPED2_XBE_VERSION}/overrides.c)
    message(FATAL_ERROR "Unknown XBE build '${AMPED2_XBE_VERSION}' in ${AMPED2_XBE_VERSION_FILE}")
endif()
message(STATUS "Amped 2 XBE build: ${AMPED2_XBE_VERSION}")

# Core game sources
set(GAME_SOURCES
    src/main.c
    src/recomp_manual.c
    src/version/${AMPED2_XBE_VERSION}/overrides.c
    src/title_image.c             # the XBE's dashboard image as the window icon
    src/settings/settings.c       # graphics settings file
    src/settings/settings_tui.c   # --settings: the settings screen in the terminal
    # Add game-specific sources here, e.g.:
    # src/asset_loader.c
    # src/custom_renderer.c
)

# The recomp pipeline also generates a dispatch header and function declarations.
# These are included by the generated .c files:
#   src/recomp/gen/recomp_funcs.h    - extern declarations for all recompiled functions
#   src/recomp/recomp_dispatch.h     - binary search dispatch table
#   src/recomp/recomp_types.h        - register model, ICALL macros, MEM helpers

# WIN32 = GUI subsystem on Windows (WinMain); ignored elsewhere.
#
# Without generated code (a fresh clone, CI -- the code comes from your own
# XBE and is never committed) there is nothing to link: build our sources as
# an object library instead, so the runtime, the host and the tests still
# compile. recomp_manual.c includes recomp/gen/recomp_types.h, which the
# recompiler copies from the toolkit's template; use the template directly.
if(RECOMP_GEN_SOURCES)
    set(AMPED2_HOST_ONLY OFF)
    add_executable(${PROJECT_NAME} WIN32
        ${GAME_SOURCES}
        ${RECOMP_GEN_SOURCES}
    )
else()
    set(AMPED2_HOST_ONLY ON)
    message(STATUS "No generated code in src/recomp/gen: host-only build "
                   "(runtime + host sources compiled, no game executable)")
    configure_file(${XBOXRECOMP_DIR}/templates/runtime/recomp_types.h
                   ${CMAKE_BINARY_DIR}/host_only/recomp/gen/recomp_types.h COPYONLY)
    add_library(${PROJECT_NAME} OBJECT ${GAME_SOURCES})
    target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR}/host_only)
endif()

# Link against the xboxrecomp umbrella target (all 6 runtime libraries)
target_link_libraries(${PROJECT_NAME} PRIVATE xboxrecomp)

# Include paths for generated recomp headers
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/recomp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/version/${AMPED2_XBE_VERSION}
    ${XBOXRECOMP_DIR}/include
)

# The exe's icon on Windows: the XBE's dashboard title image, extracted from
# the disc by run_pipeline.sh (scripts/xbe_icon.py) next to the generated
# code -- never committed. Opt-in, for local builds only: an exe that is
# published must not carry the game's artwork. The window and taskbar icon
# needs none of this -- src/title_image.c decodes it at run time from the
# player's own default.xbe.
option(AMPED2_EMBED_DISC_ICON "Embed the disc's title image as the exe icon (local builds only, never for a release)" OFF)
set(AMPED2_ICON ${CMAKE_CURRENT_SOURCE_DIR}/src/recomp/gen/amped2.ico)
if(WIN32 AND AMPED2_EMBED_DISC_ICON AND NOT AMPED2_HOST_ONLY AND EXISTS ${AMPED2_ICON})
    enable_language(RC)
    file(WRITE ${CMAKE_BINARY_DIR}/amped2_icon.rc "1 ICON \"${AMPED2_ICON}\"\n")
    target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR}/amped2_icon.rc)
endif()

if(WIN32)
    # MinGW's libxinput.a imports xinput1_3.dll, which only the DirectX 2010
    # redistributable installs; xinput1_4.dll ships with Windows 8 and later.
    # MSVC's xinput.lib already means xinput1_4.
    set(AMPED2_XINPUT xinput)
    if(MINGW)
        set(AMPED2_XINPUT xinput1_4)
        get_target_property(AMPED2_INPUT_LINK xbox_input INTERFACE_LINK_LIBRARIES)
        list(TRANSFORM AMPED2_INPUT_LINK REPLACE "^xinput$" xinput1_4)
        set_property(TARGET xbox_input PROPERTY INTERFACE_LINK_LIBRARIES ${AMPED2_INPUT_LINK})
    endif()
    # Windows SDK libraries needed at link time
    target_link_libraries(${PROJECT_NAME} PRIVATE
        d3d11
        dxgi
        dxguid
        ${AMPED2_XINPUT}
        winmm
        # Lets the fault handler resolve the host RIP back to the generated
        # function's symbol, i.e. to the guest function that faulted.
        dbghelp
    )
else()
    # POSIX host: SDL2/epoxy come in through the runtime targets. The crash
    # handler names the faulting guest function with dladdr, which only sees
    # exported symbols -> export the executable's symbols (-rdynamic).
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
    set_target_properties(${PROJECT_NAME} PROPERTIES ENABLE_EXPORTS ON)
endif()

# ── Vulkan render back end (src/render, docs/vulkan-backend.md) ──
#
# Vulkan-Headers and volk are pinned submodules under third_party/, so no
# Vulkan SDK is needed: volk loads libvulkan / vulkan-1.dll at run time. Only
# glslc is needed at build time (Linux: the glslc package; Windows: the
# Vulkan SDK, found through VULKAN_SDK). The window comes from SDL2: the
# system's on POSIX, on Windows the pinned submodule third_party/SDL built
# statically into the exe (no SDL2.dll to ship). Selected at run time with
# AMPED2_RENDERER=vulkan (the default on Windows, see apply_windows_defaults).
option(AMPED2_VULKAN "Build the Vulkan render back end" ON)
if(AMPED2_VULKAN AND WIN32 AND NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL/CMakeLists.txt)
    message(FATAL_ERROR "SDL2 submodule missing: git submodule update --init third_party/SDL "
                        "(or configure with -DAMPED2_VULKAN=OFF for the software renderer only)")
endif()
if(AMPED2_VULKAN)
    set(AMPED2_VK_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Vulkan-Headers/include)
    set(AMPED2_VOLK_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/third_party/volk)
    if(NOT EXISTS ${AMPED2_VK_HEADERS}/vulkan/vulkan_core.h OR NOT EXISTS ${AMPED2_VOLK_DIR}/volk.c)
        message(FATAL_ERROR "Vulkan submodules missing: git submodule update --init third_party/Vulkan-Headers third_party/volk "
                            "(or configure with -DAMPED2_VULKAN=OFF)")
    endif()
    add_library(amped2_volk STATIC ${AMPED2_VOLK_DIR}/volk.c)
    target_include_directories(amped2_volk PUBLIC ${AMPED2_VOLK_DIR} ${AMPED2_VK_HEADERS})
    target_compile_definitions(amped2_volk PUBLIC VK_NO_PROTOTYPES)
    target_link_libraries(amped2_volk PRIVATE ${CMAKE_DL_LIBS})

    # Shaders: GLSL -> SPIR-V as C initialiser lists, included by vk_backend.c.
    find_program(AMPED2_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin" REQUIRED)
    set(AMPED2_SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
    set(AMPED2_SHADER_INCS)
    foreach(sh nv2a.vert nv2a.frag nv2a_vsh.vert)
        add_custom_command(
            OUTPUT  ${AMPED2_SHADER_OUT}/${sh}.inc
            COMMAND ${CMAKE_COMMAND} -E make_directory ${AMPED2_SHADER_OUT}
            COMMAND ${AMPED2_GLSLC} --target-env=vulkan1.3 -O -mfmt=c
                    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/${sh}
                    -o ${AMPED2_SHADER_OUT}/${sh}.inc
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/${sh}
            COMMENT "glslc ${sh}")
        list(APPEND AMPED2_SHADER_INCS ${AMPED2_SHADER_OUT}/${sh}.inc)
    endforeach()
    add_custom_target(amped2_shaders DEPENDS ${AMPED2_SHADER_INCS})
    add_dependencies(${PROJECT_NAME} amped2_shaders)

    if(WIN32)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST OFF CACHE BOOL "" FORCE)
        set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
        set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
        add_subdirectory(third_party/SDL ${CMAKE_BINARY_DIR}/SDL EXCLUDE_FROM_ALL)
        set(AMPED2_SDL2 SDL2::SDL2-static)
        # The runtime builds its SDL keyboard glue (SDL key events into the
        # pad's key table) on POSIX only; the Vulkan window needs it here too.
        target_sources(${PROJECT_NAME} PRIVATE ${XBOXRECOMP_DIR}/src/input/keyboard_sdl.c)
        # Our game keys (Space/Escape/Tab, src/input/amped2_keys.c, built into
        # xbox_input) read SDL's keyboard state as well as the Win32 one.
        target_compile_definitions(xbox_input PRIVATE AMPED2_SDL_KEYS=1)
        target_link_libraries(xbox_input PRIVATE SDL2::SDL2-static)
    else()
        find_package(SDL2 REQUIRED)
        set(AMPED2_SDL2 SDL2::SDL2)
    endif()
    target_sources(${PROJECT_NAME} PRIVATE src/render/vk_backend.c)
    target_include_directories(${PROJECT_NAME} PRIVATE ${AMPED2_SHADER_OUT})
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${XBOXRECOMP_DIR}/src/kernel     # nv2a_backend.h
        ${XBOXRECOMP_DIR}/src/input)     # keyboard_sdl.h
    target_link_libraries(${PROJECT_NAME} PRIVATE amped2_volk ${AMPED2_SDL2})
    target_compile_definitions(${PROJECT_NAME} PRIVATE AMPED2_HAVE_VULKAN=1)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # Guest memory is read and written through differently typed pointers
    # (MEM8/16/32, floats, structs) at the same addresses. MSVC never assumes
    # strict aliasing; GCC/Clang do at -O2, so turn it off to match.
    target_compile_options(${PROJECT_NAME} PRIVATE -fno-strict-aliasing)
endif()

# MSVC-specific: large address aware, increase section limit for big generated files
if(MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE
        /LARGEADDRESSAWARE
    )
    # The generated code files can be very large. Increase object file section limit.
    target_compile_options(${PROJECT_NAME} PRIVATE /bigobj)
elseif(MINGW)
    # Same section limit for MinGW's assembler.
    target_compile_options(${PROJECT_NAME} PRIVATE -Wa,-mbig-obj)
endif()

# ── Host tests (ctest) ──────────────────────────────────────────
# No game data needed; tests/CMakeLists.txt lists them.
option(AMPED2_TESTS "Build the host tests (run with ctest)" OFF)
if(AMPED2_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
