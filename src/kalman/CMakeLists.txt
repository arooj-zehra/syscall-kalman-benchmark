# Phase 5: synthetic data generator and the serial Kalman filter.
#
#   kfcore       static library: filter, generator, file format (links oscore)
#   kfgen        write a measurement file
#   kf_serial    serial reference filter
#   kf_eval      error of a result file against the regenerated truth
#   test_kalman  unit tests            kf_check   statistical consistency test
#   kf_pipeline  end-to-end test (generate, filter twice, evaluate, Python reference)

add_library(kfcore STATIC kalman.c datagen.c kfio.c)
target_link_libraries(kfcore PUBLIC oscore)
target_include_directories(kfcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(NOT MSVC)
  # Never fuse a*b+c: serial and parallel runs, and both operating systems, must
  # give identical bits.
  target_compile_options(kfcore PRIVATE -ffp-contract=off)
endif()

foreach(tool kfgen kf_serial kf_eval kf_check test_kalman)
  add_executable(${tool} ${tool}.c)
  target_link_libraries(${tool} PRIVATE kfcore)
  if(UNIX)
    target_link_libraries(${tool} PRIVATE m)
  endif()
  if(NOT MSVC)
    target_compile_options(${tool} PRIVATE -ffp-contract=off)
  endif()
endforeach()

add_test(NAME kf_unit COMMAND test_kalman)
add_test(NAME kf_consistency COMMAND kf_check)

# The independent Python check needs numpy; the rest of the pipeline does not.
set(KF_PYTHON "")
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
  execute_process(COMMAND ${Python3_EXECUTABLE} -c "import numpy" RESULT_VARIABLE NUMPY_RC
                  OUTPUT_QUIET ERROR_QUIET)
  if(NUMPY_RC EQUAL 0)
    set(KF_PYTHON ${Python3_EXECUTABLE})
  endif()
endif()
if(NOT KF_PYTHON)
  message(STATUS "kalman: Python with numpy not found; kf_pipeline will skip the Python reference")
endif()

add_test(NAME kf_pipeline
  COMMAND ${CMAKE_COMMAND}
    -DKFGEN=$<TARGET_FILE:kfgen> -DKF_SERIAL=$<TARGET_FILE:kf_serial> -DKF_EVAL=$<TARGET_FILE:kf_eval>
    -DPYTHON=${KF_PYTHON} -DREF=${CMAKE_SOURCE_DIR}/tools/kf_reference.py
    -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/pipeline
    -P ${CMAKE_CURRENT_SOURCE_DIR}/pipeline_test.cmake)
set_tests_properties(kf_pipeline PROPERTIES TIMEOUT 300)
