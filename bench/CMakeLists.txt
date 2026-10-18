# Phase 4: benchmark harness.
#
#   bench       the driver (build/bin/bench[.exe]); starts one fresh process per case
#   test_stats  unit test of the statistics (runs under ctest)
#   bench_quick smoke test of the whole harness with tiny sizes (runs under ctest)

set(HELPER_DEFINE "HELPER_NAME=\"child_helper${CMAKE_EXECUTABLE_SUFFIX}\"")

if(WIN32)
  set(PLATFORM_CASES cases_platform_win.c)
else()
  set(PLATFORM_CASES cases_platform_linux.c)
endif()

add_executable(bench
  bench_main.c
  bench_stats.c
  bench_util.c
  cases_baseline.c
  cases_fileio.c
  cases_process.c
  cases_memory.c
  cases_ipc.c
  cases_perms.c
  ${PLATFORM_CASES}
)
target_link_libraries(bench PRIVATE oscore)
target_include_directories(bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(bench PRIVATE ${HELPER_DEFINE})
add_dependencies(bench child_helper)
if(UNIX)
  target_link_libraries(bench PRIVATE m)
endif()

add_executable(test_stats test_stats.c bench_stats.c)
target_include_directories(test_stats PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(UNIX)
  target_link_libraries(test_stats PRIVATE m)
endif()
add_test(NAME bench_stats COMMAND test_stats)

# Whole-harness smoke test: every case once with tiny sizes. The numbers are
# meaningless (--quick); this only proves that every call works and the CSV is written.
set(QUICK_CSV ${CMAKE_BINARY_DIR}/bench_quick.csv)
add_test(NAME bench_quick
  COMMAND ${CMAKE_COMMAND} -E remove -f ${QUICK_CSV})
add_test(NAME bench_quick_run
  COMMAND bench --quick --out ${QUICK_CSV} --tmpdir ${CMAKE_BINARY_DIR})
set_tests_properties(bench_quick_run PROPERTIES TIMEOUT 300 DEPENDS bench_quick)
