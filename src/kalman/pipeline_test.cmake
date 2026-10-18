# End-to-end test, run by ctest as:  cmake -DKFGEN=... -DKF_SERIAL=... -DKF_EVAL=...
#                                          -DPYTHON=... -DREF=... -DWORKDIR=... -P pipeline_test.cmake
#
#   1. generate a data set twice            -> identical hash and identical bytes
#   2. serial filter with mmap and with read -> identical hash and identical bytes
#   3. kf_eval --check                       -> error matches the filter's own prediction
#   4. Python reference (if given)           -> generator bit-identical, filter within 1e-6

file(MAKE_DIRECTORY "${WORKDIR}")

function(run_step name)
  execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${WORKDIR}"
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${name} failed (exit ${rc}):\n${out}\n${err}")
  endif()
  message(STATUS "${name}: ok")
  set(LAST_OUTPUT "${out}" PARENT_SCOPE)
endfunction()

function(extract_hash text key result)
  string(REGEX MATCH "${key}=0x[0-9a-f]+" m "${text}")
  if(NOT m)
    message(FATAL_ERROR "no ${key} in output:\n${text}")
  endif()
  set(${result} "${m}" PARENT_SCOPE)
endfunction()

set(ARGS_GEN --tracks 8 --steps 600 --seed 7)

run_step("generate A" "${KFGEN}" ${ARGS_GEN} --out pipe_meas_a.bin)
extract_hash("${LAST_OUTPUT}" data_hash HASH_A)
run_step("generate B" "${KFGEN}" ${ARGS_GEN} --out pipe_meas_b.bin)
extract_hash("${LAST_OUTPUT}" data_hash HASH_B)
if(NOT HASH_A STREQUAL HASH_B)
  message(FATAL_ERROR "generator is not deterministic: ${HASH_A} vs ${HASH_B}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files pipe_meas_a.bin pipe_meas_b.bin
                WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE same)
if(NOT same EQUAL 0)
  message(FATAL_ERROR "two generated files differ")
endif()

run_step("serial, mmap input" "${KF_SERIAL}" --in pipe_meas_a.bin --out pipe_est_mmap.bin --io mmap --verify-input)
extract_hash("${LAST_OUTPUT}" est_hash EST_MMAP)
run_step("serial, read input" "${KF_SERIAL}" --in pipe_meas_a.bin --out pipe_est_read.bin --io read --verify-input)
extract_hash("${LAST_OUTPUT}" est_hash EST_READ)
if(NOT EST_MMAP STREQUAL EST_READ)
  message(FATAL_ERROR "mmap and read input give different results: ${EST_MMAP} vs ${EST_READ}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files pipe_est_mmap.bin pipe_est_read.bin
                WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE same)
if(NOT same EQUAL 0)
  message(FATAL_ERROR "estimate files differ between mmap and read input")
endif()

run_step("evaluate" "${KF_EVAL}" --meas pipe_meas_a.bin --est pipe_est_mmap.bin --check)
message(STATUS "${LAST_OUTPUT}")

if(PYTHON)
  run_step("python reference" "${PYTHON}" "${REF}" check --meas pipe_meas_a.bin --est pipe_est_mmap.bin --tracks 3)
  message(STATUS "${LAST_OUTPUT}")
else()
  message(STATUS "python reference: skipped (no Python with numpy)")
endif()

file(REMOVE pipe_meas_a.bin pipe_meas_b.bin pipe_est_mmap.bin pipe_est_read.bin)
