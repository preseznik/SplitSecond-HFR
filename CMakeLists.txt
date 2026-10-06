cmake_minimum_required(VERSION 3.25)
project(SplitSecondHFR VERSION 0.1.1 LANGUAGES C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Split/Second is a 32-bit game: configure with -A Win32")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(GAME_DIR "C:/Program Files (x86)/Steam/steamapps/common/SplitSecond" CACHE PATH "Split/Second install folder")
option(HFR_DEVTOOLS "Build the developer instrumentation (not part of the public source tree)" OFF)

# --- SafetyHook (vendored amalgamated release, BSL-1.0; includes Zydis, MIT) ---
add_library(safetyhook STATIC
  third_party/safetyhook/safetyhook.cpp
  third_party/safetyhook/Zydis.c)
target_include_directories(safetyhook PUBLIC third_party/safetyhook)
target_compile_options(safetyhook PRIVATE /W0)

# --- The mod: loaded by the game itself as DXFreezerServer.dll ---
set(HFR_SOURCES
  src/core/dllmain.cpp
  src/core/config.cpp
  src/core/log.cpp
  src/core/util.cpp
  src/core/version_gate.cpp
  src/core/patchset.cpp
  src/core/iat.cpp
  src/core/frames.cpp
  src/core/exports.def
  src/timing/limiter.cpp
  src/fixes/fix_ai30.cpp
  src/d3d/d3d9_hooks.cpp)

set(HFR_DEV_SOURCES
  src/instr/crash.cpp
  src/instr/stats.cpp
  src/instr/api_hooks.cpp
  src/instr/sampler.cpp
  src/instr/dumper.cpp
  src/instr/probes.cpp
  src/instr/memstats.cpp)

if(HFR_DEVTOOLS)
  foreach(f IN LISTS HFR_DEV_SOURCES)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${f}")
      message(FATAL_ERROR "HFR_DEVTOOLS=ON but ${f} is not part of this source tree")
    endif()
  endforeach()
  list(APPEND HFR_SOURCES ${HFR_DEV_SOURCES})
endif()

add_library(DXFreezerServer SHARED ${HFR_SOURCES})
target_link_libraries(DXFreezerServer PRIVATE safetyhook)
if(HFR_DEVTOOLS)
  target_link_libraries(DXFreezerServer PRIVATE dbghelp psapi)
  target_compile_definitions(DXFreezerServer PRIVATE HFR_DEVTOOLS=1)
endif()
target_compile_definitions(DXFreezerServer PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
  HFR_VERSION="${PROJECT_VERSION}")
target_compile_options(DXFreezerServer PRIVATE /W4 /permissive- /utf-8 /Zi /EHsc)
target_link_options(DXFreezerServer PRIVATE /DEBUG /OPT:REF /OPT:ICF /SAFESEH:NO /PDBALTPATH:DXFreezerServer.pdb)

# Copies the DLL (and the INI if the game folder has none yet) into the game folder.
add_custom_target(deploy
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tools/deploy.ps1"
          -Dll "$<TARGET_FILE:DXFreezerServer>" -GameDir "${GAME_DIR}" -Ini "${CMAKE_SOURCE_DIR}/config/SplitSecondHFR.ini"
  DEPENDS DXFreezerServer
  VERBATIM)
