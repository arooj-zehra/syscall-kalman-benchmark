cmake_minimum_required(VERSION 3.20)
project(syscall_kalman_benchmark LANGUAGES C)

# Must come before any add_subdirectory() that registers tests.
enable_testing()

# ---------------------------------------------------------------------------
# Language and build settings (identical on Linux and Windows)
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Benchmarks must be optimised. Default to Release for single-config generators.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---------------------------------------------------------------------------
# oscore: logging + platform layer. Every later program links against this.
# ---------------------------------------------------------------------------
if(WIN32)
  set(PLATFORM_SRC src/win/platform_win.c)
else()
  set(PLATFORM_SRC src/linux/platform_linux.c)
endif()

add_library(oscore STATIC
  src/common/log.c
  ${PLATFORM_SRC}
)
target_include_directories(oscore PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(oscore PUBLIC "BUILD_TYPE=\"${CMAKE_BUILD_TYPE}\"")

if(MSVC)
  target_compile_options(oscore PUBLIC /W4)
else()
  target_compile_options(oscore PUBLIC -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
  # advapi32: registry and SetFileSecurity. psapi: memory usage (Phase 4).
  target_compile_definitions(oscore PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
  target_link_libraries(oscore PUBLIC advapi32 psapi)
else()
  target_compile_definitions(oscore PUBLIC _GNU_SOURCE)
endif()

# Wrapped system calls (platform.h, Phase 2+). The Windows file arrives in Phase 3.
if(UNIX)
  target_sources(oscore PRIVATE src/linux/sys_linux.c)
endif()

if(MINGW)
  # Static runtime so the .exe files run from plain cmd without MSYS2 DLLs on PATH.
  target_link_options(oscore PUBLIC -static)
endif()

# ---------------------------------------------------------------------------
# Programs
# ---------------------------------------------------------------------------
add_executable(hello src/hello.c)
target_link_libraries(hello PRIVATE oscore)

# System call demos. Linux now; the Windows build joins in Phase 3 once
# src/win/sys_win.c exists (then replace this if/else with a plain add_subdirectory).
if(UNIX)
  add_subdirectory(src/syscalls)
else()
  message(STATUS "System call demos skipped on Windows until Phase 3 (src/win/sys_win.c)")
endif()

# Added in later phases (uncomment when the folder has sources):
# add_subdirectory(bench)          # Phase 4: benchmark harness
# add_subdirectory(src/kalman)     # Phases 5-6: serial filter, master, workers

# ---------------------------------------------------------------------------
# Tests: `ctest --test-dir build --output-on-failure`
# ---------------------------------------------------------------------------
add_test(NAME hello COMMAND hello ${CMAKE_BINARY_DIR}/hello.log)
