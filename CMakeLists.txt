cmake_minimum_required(VERSION 3.30)

project(neuro_fnv LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enforce 32-bit Windows targets
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "neuro-fnv must be built as a 32-bit (x86) Windows target.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -----------------------------------------------------------------------------
# 1. External Dependencies Setup
# -----------------------------------------------------------------------------
set(NEURO_BUILD_STATIC ON CACHE BOOL "Build libneurosdk statically" FORCE)
add_subdirectory(libs/libneurosdk)

set_target_properties(neurosdk PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
target_compile_definitions(neurosdk PRIVATE _CRT_RAND_S _GNU_SOURCE)

# -----------------------------------------------------------------------------
# 2. Main Plugin Target Setup
# -----------------------------------------------------------------------------
file(GLOB_RECURSE NEURO_FNV_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*/*.cpp"
)

file(GLOB XNVSE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/common/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/nvse/*.cpp"
)
add_library(neuro-fnv SHARED ${NEURO_FNV_SOURCES} ${XNVSE_SOURCES})

set_target_properties(neuro-fnv PROPERTIES
    OUTPUT_NAME "neuro-fnv"
    PREFIX ""
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

# -----------------------------------------------------------------------------
# 3. Compiler Options, Definitions & Includes
# -----------------------------------------------------------------------------
target_include_directories(neuro-fnv PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/libneurosdk/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/nvse"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/nvse/nvse"
)
target_sources(neuro-fnv PRIVATE
    libs/nvse/nvse/containers.cpp
    libs/nvse/nvse/GameAPI.cpp
    libs/nvse/nvse/GameBSExtraData.cpp
    libs/nvse/nvse/GameData.cpp
    libs/nvse/nvse/GameExtraData.cpp
    libs/nvse/nvse/GameForms.cpp
    libs/nvse/nvse/GameObjects.cpp
    libs/nvse/nvse/GameOSDepend.cpp
    libs/nvse/nvse/GameProcess.cpp
    libs/nvse/nvse/GameRTTI.cpp
    libs/nvse/nvse/GameScript.cpp
    libs/nvse/nvse/GameSettings.cpp
    libs/nvse/nvse/GameTasks.cpp
    libs/nvse/nvse/GameTiles.cpp
    libs/nvse/nvse/GameTypes.cpp
    libs/nvse/nvse/GameUI.cpp
    libs/nvse/nvse/NiTypes.cpp
    libs/nvse/nvse/printf.cpp
    libs/nvse/nvse/SafeWrite.cpp
    libs/nvse/nvse/Utilities.cpp
    libs/nvse/nvse/utility.cpp
)
target_compile_definitions(neuro-fnv PRIVATE
    RUNTIME
    RUNTIME_VERSION=0x040020D0
    _WINDOWS
    _USRDLL
)

# Force MSVC to resolve functions, relax compliance, and explicitly force-load 
# the master prefix header directly into every translation unit.
target_compile_options(neuro-fnv PRIVATE 
    /permissive
    "/FI${CMAKE_CURRENT_SOURCE_DIR}/libs/nvse/nvse/prefix.h"
)

# -----------------------------------------------------------------------------
# 4. Linker Libraries
# -----------------------------------------------------------------------------
target_link_libraries(neuro-fnv PRIVATE 
    neurosdk 
    ws2_32.lib 
    advapi32.lib 
    shell32.lib 
    comdlg32.lib 
    winmm.lib
)

# -----------------------------------------------------------------------------
# 5. Automated Deployment Setup
# -----------------------------------------------------------------------------
set(NEURO_FNV_PLUGIN_ROOT "$ENV{FalloutNVNeuroPlugin}")
if(NEURO_FNV_PLUGIN_ROOT)
    set(PLUGIN_DIR "${NEURO_FNV_PLUGIN_ROOT}/nvse/plugins")
    
    add_custom_command(TARGET neuro-fnv POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${PLUGIN_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:neuro-fnv>" "${PLUGIN_DIR}/$<TARGET_FILE_NAME:neuro-fnv>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:neuro-fnv>" "${PLUGIN_DIR}/$<TARGET_PDB_FILE_NAME:neuro-fnv>"
        COMMENT "Deploying compiled artifacts natively to New Vegas environment..."
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
endif()
