# Parallel filter test, run by ctest as:
#   cmake -DKFGEN=... -DKF_SERIAL=... -DKF_MASTER=... -DWORKDIR=... -P parallel_test.cmake
#
#   1. the serial result is the reference
#   2. the master with 1, 2, 3, 5, 8 workers (5 does not divide 11 tracks) gives the
#      same hash and a byte-identical file; --expect-file turns a mismatch into a failure
#   3. a deliberately wrong reference is rejected and writes no CSV row
#   4. a worker that dies at start, or in the middle of a round, makes the master
#      fail cleanly (non-zero exit, no CSV row, no hang)
#   5. more workers than tracks and a missing input are refused

file(MAKE_DIRECTORY "${WORKDIR}")
file(REMOVE "${WORKDIR}/par.csv" "${WORKDIR}/bad.csv")

function(run_step name expect_rc)
  execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc
                  OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
  if(rc STREQUAL "Process terminated due to timeout")
    message(FATAL_ERROR "${name}: timed out (a worker was probably left running)")
  endif()
  if(expect_rc STREQUAL "ok" AND NOT rc EQUAL 0)
    message(FATAL_ERROR "${name} failed (exit ${rc}):\n${out}\n${err}")
  endif()
  if(expect_rc STREQUAL "fail" AND rc EQUAL 0)
    message(FATAL_ERROR "${name} should have failed but succeeded:\n${out}")
  endif()
  message(STATUS "${name}: ok")
  set(LAST_OUTPUT "${out}" PARENT_SCOPE)
endfunction()

function(extract_hash text result)
  string(REGEX MATCH "est_hash=0x[0-9a-f]+" m "${text}")
  if(NOT m)
    message(FATAL_ERROR "no est_hash in output:\n${text}")
  endif()
  set(${result} "${m}" PARENT_SCOPE)
endfunction()

# 11 tracks: not divisible by 2, 3, 5 or 8, so the blocks have different sizes.
run_step("generate" ok "${KFGEN}" --tracks 11 --steps 1500 --seed 3 --out par_meas.bin)
run_step("serial reference" ok "${KF_SERIAL}" --in par_meas.bin --out par_est_serial.bin)
extract_hash("${LAST_OUTPUT}" SERIAL_HASH)

foreach(w 1 2 3 5 8)
  run_step("master, ${w} worker(s)" ok "${KF_MASTER}" --in par_meas.bin --workers ${w} --repeat 2
           --out par_est_${w}.bin --expect-file par_est_serial.bin --csv par.csv)
  extract_hash("${LAST_OUTPUT}" H)
  if(NOT H STREQUAL SERIAL_HASH)
    message(FATAL_ERROR "${w} workers: ${H} differs from serial ${SERIAL_HASH}")
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files par_est_serial.bin par_est_${w}.bin
                  WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE same)
  if(NOT same EQUAL 0)
    message(FATAL_ERROR "${w} workers: output file is not byte-identical to the serial file")
  endif()
endforeach()

# Same hash on every run (no race between workers).
foreach(n RANGE 1 5)
  run_step("repeat ${n}" ok "${KF_MASTER}" --in par_meas.bin --workers 4 --expect-file par_est_serial.bin)
endforeach()

# A wrong reference must be rejected. Different seed -> different data -> different hash.
run_step("generate other" ok "${KFGEN}" --tracks 11 --steps 1500 --seed 4 --out par_meas_other.bin)
run_step("serial other" ok "${KF_SERIAL}" --in par_meas_other.bin --out par_est_other.bin)
run_step("wrong reference is rejected" fail "${KF_MASTER}" --in par_meas.bin --workers 3
         --expect-file par_est_other.bin --csv bad.csv)

foreach(stage start compute)
  run_step("worker dies (${stage})" fail "${KF_MASTER}" --in par_meas.bin --workers 4
           --inject-failure ${stage} --csv bad.csv)
endforeach()
if(EXISTS "${WORKDIR}/bad.csv")
  message(FATAL_ERROR "a failed run wrote a CSV row")
endif()

run_step("more workers than tracks is refused" fail "${KF_MASTER}" --in par_meas.bin --workers 12)
run_step("missing input is refused" fail "${KF_MASTER}" --in no_such_file.bin --workers 2)

# The CSV has one row per good run, all verified.
file(READ "${WORKDIR}/par.csv" CSV)
string(REPLACE "\n" ";" rows "${CSV}")
list(FILTER rows INCLUDE REGEX ",yes$")
list(LENGTH rows n_rows)
if(NOT n_rows EQUAL 5)
  message(FATAL_ERROR "expected 5 verified CSV rows, found ${n_rows}:\n${CSV}")
endif()
message(STATUS "parallel: all checks passed")
