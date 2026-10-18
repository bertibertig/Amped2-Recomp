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

# Core game sources
set(GAME_SOURCES
    src/main.c
    src/recomp_manual.c
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
add_executable(${PROJECT_NAME} WIN32
    ${GAME_SOURCES}
    ${RECOMP_GEN_SOURCES}
)

# Link against the xboxrecomp umbrella target (all 6 runtime libraries)
target_link_libraries(${PROJECT_NAME} PRIVATE xboxrecomp)

# Include paths for generated recomp headers
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/recomp
    ${XBOXRECOMP_DIR}/include
)

if(WIN32)
    # Windows SDK libraries needed at link time
    target_link_libraries(${PROJECT_NAME} PRIVATE
        d3d11
        dxgi
        dxguid
        xinput
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

# ── Render back ends ────────────────────────────────────────
#
# For now only the executor's CPU rasteriser (multi-threaded on POSIX through
# the runtime patch, RECOMP_RASTER_THREADS). The Vulkan back end from
# SSX3-Recomp (src/render/, AMPED2_RENDERER=vulkan) comes once the CPU path
# draws the game correctly -- it is the reference to compare against.

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
