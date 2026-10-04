# Cross-compilation through the x86 wrappers installed by msvc-wine.
set(NEURO_FNV_MSVC_ROOT "$ENV{NEURO_FNV_MSVC_ROOT}" CACHE PATH "Root of the msvc-wine installation")
if(NOT NEURO_FNV_MSVC_ROOT)
    set(NEURO_FNV_MSVC_ROOT "/opt/msvc" CACHE PATH "Root of the msvc-wine installation" FORCE)
endif()
get_filename_component(NEURO_FNV_MSVC_ROOT "${NEURO_FNV_MSVC_ROOT}" ABSOLUTE)

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR i686)
# Probe compilation/linking without debug records: Wine/MSVC Debug can fail
# independently of compiler usability. Real Debug targets still use debug info.
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
# Avoid separate compile-PDB RPC where supported; linker PDBs remain available.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>"
    CACHE STRING "MSVC compile debug information format")
set(CMAKE_C_COMPILER "${NEURO_FNV_MSVC_ROOT}/bin/x86/cl")
set(CMAKE_CXX_COMPILER "${NEURO_FNV_MSVC_ROOT}/bin/x86/cl")
set(CMAKE_RC_COMPILER "${NEURO_FNV_MSVC_ROOT}/bin/x86/rc")
set(CMAKE_LINKER "${NEURO_FNV_MSVC_ROOT}/bin/x86/link")
set(CMAKE_FIND_ROOT_PATH "${NEURO_FNV_MSVC_ROOT}/cmake/find_root/x86")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
