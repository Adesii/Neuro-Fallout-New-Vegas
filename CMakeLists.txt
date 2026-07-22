cmake_minimum_required(VERSION 3.30)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(neuro_fnv LANGUAGES C CXX)

set(NEURO_FNV_COMPILE_COMMANDS
    "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json")
if(IS_SYMLINK "${NEURO_FNV_COMPILE_COMMANDS}")
    file(REMOVE "${NEURO_FNV_COMPILE_COMMANDS}")
endif()
if(NOT EXISTS "${NEURO_FNV_COMPILE_COMMANDS}")
    file(CREATE_LINK "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${NEURO_FNV_COMPILE_COMMANDS}" SYMBOLIC)
else()
    message(WARNING
        "${NEURO_FNV_COMPILE_COMMANDS} already exists and will not be replaced.")
endif()

# Fallout New Vegas and xNVSE are 32-bit Windows processes. A Linux shared
# object cannot be loaded by Wine in their place.
if(NOT WIN32)
    message(FATAL_ERROR
        "neuro-fnv must be built for 32-bit Windows. Use the windows-i686-debug "
        "or windows-i686-release CMake preset.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "neuro-fnv must be built as a 32-bit (i686) DLL.")
endif()

set(NEURO_BUILD_STATIC ON CACHE BOOL "Build libneurosdk statically" FORCE)
add_subdirectory(libs/libneurosdk)
target_compile_definitions(neurosdk PRIVATE
    _CRT_RAND_S
    _GNU_SOURCE
    __USE_MINGW_ANSI_STDIO=1
)

file(GLOB_RECURSE NEURO_FNV_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
)

add_library(neuro-fnv SHARED ${NEURO_FNV_SOURCES})
target_sources(neuro-fnv PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/xnvse/common/IDebugLog.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/xnvse/common/IFileStream.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/xnvse/common/IDataStream.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/xnvse/common/IErrors.cpp"
)
set_target_properties(neuro-fnv PROPERTIES
    OUTPUT_NAME "neuro-fnv"
    PREFIX ""
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    CXX_SCAN_FOR_MODULES NO
)

# xNVSE headers require its repository root: nvse/ includes files from common/.
target_include_directories(neuro-fnv PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/compat"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/xnvse"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/xnvse/nvse"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/libneurosdk/include"
)
target_compile_definitions(neuro-fnv PRIVATE
    RUNTIME
    RUNTIME_VERSION=0x040020D0
)
target_compile_options(neuro-fnv PRIVATE
    "-include${CMAKE_CURRENT_SOURCE_DIR}/cmake/compat/xnvse-prefix.h"
)

target_link_libraries(neuro-fnv PRIVATE neurosdk ws2_32 advapi32 shell32 comdlg32)
target_link_options(neuro-fnv PRIVATE -static -static-libgcc -static-libstdc++)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(NEURO_FNV_PDB "${CMAKE_CURRENT_BINARY_DIR}/neuro-fnv.pdb")
    target_compile_options(neuro-fnv PRIVATE -g)
    target_link_options(neuro-fnv PRIVATE "-Wl,--pdb,${NEURO_FNV_PDB}")
endif()

set(NEURO_FNV_PLUGIN_ROOT "$ENV{FalloutNVNeuroPlugin}")
if(NEURO_FNV_PLUGIN_ROOT)
    set(NEURO_FNV_PLUGIN_DESTINATION
        "${NEURO_FNV_PLUGIN_ROOT}/nvse/plugins/$<TARGET_FILE_NAME:neuro-fnv>")
    add_custom_command(TARGET neuro-fnv POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${NEURO_FNV_PLUGIN_ROOT}/nvse/plugins"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:neuro-fnv>" "${NEURO_FNV_PLUGIN_DESTINATION}"
        COMMENT "Copying neuro-fnv.dll to ${NEURO_FNV_PLUGIN_ROOT}/nvse/plugins"
        VERBATIM
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_command(TARGET neuro-fnv POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${NEURO_FNV_PDB}"
                "${NEURO_FNV_PLUGIN_ROOT}/nvse/plugins/neuro-fnv.pdb"
            COMMENT "Copying neuro-fnv.pdb to ${NEURO_FNV_PLUGIN_ROOT}/nvse/plugins"
            VERBATIM
        )
    endif()
else()
    message(STATUS
        "FalloutNVNeuroPlugin is not set; neuro-fnv.dll will not be deployed automatically.")
endif()
