# Phases 2-3: one small demo program per system-call group, plus the helper
# program the process and IPC demos start.
#
# Every demo exits 0 only if all of its checks pass, so `ctest` runs them all.

set(HELPER_DEFINE "HELPER_NAME=\"child_helper${CMAKE_EXECUTABLE_SUFFIX}\"")

add_executable(child_helper child_helper.c)
target_link_libraries(child_helper PRIVATE oscore)
target_include_directories(child_helper PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set(DEMOS fileio process memory ipc perms)
if(UNIX)
  # fork/exec/wait shown separately; Windows has no fork().
  list(APPEND DEMOS process_linux)
endif()

foreach(demo IN LISTS DEMOS)
  add_executable(demo_${demo} demo_${demo}.c)
  target_link_libraries(demo_${demo} PRIVATE oscore)
  target_include_directories(demo_${demo} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(demo_${demo} PRIVATE ${HELPER_DEFINE})
  add_dependencies(demo_${demo} child_helper)
  add_test(NAME demo_${demo} COMMAND demo_${demo} ${CMAKE_BINARY_DIR}/demo_${demo}.log)
  set_tests_properties(demo_${demo} PROPERTIES TIMEOUT 60)
endforeach()
