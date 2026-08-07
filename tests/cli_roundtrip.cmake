if(NOT DEFINED CLI)
    message(FATAL_ERROR "CLI executable path is required")
endif()
if(NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "WORK_DIR is required")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(WRITE "${WORK_DIR}/input.txt" "hello from cli integration\nsecond line\n")
file(WRITE "${WORK_DIR}/password.txt" "correct horse\n")
file(WRITE "${WORK_DIR}/wrong-password.txt" "wrong horse\n")

execute_process(
    COMMAND "${CLI}" pack "${WORK_DIR}/input.txt" "${WORK_DIR}/output.txx" --password-stdin
    INPUT_FILE "${WORK_DIR}/password.txt"
    RESULT_VARIABLE pack_result
    OUTPUT_VARIABLE pack_stdout
    ERROR_VARIABLE pack_stderr
)
if(NOT pack_result EQUAL 0)
    message(FATAL_ERROR "CLI pack failed (${pack_result}): ${pack_stdout}${pack_stderr}")
endif()

execute_process(
    COMMAND "${CLI}" unpack "${WORK_DIR}/output.txx" "${WORK_DIR}/restored.txt" --password-stdin
    INPUT_FILE "${WORK_DIR}/password.txt"
    RESULT_VARIABLE unpack_result
    OUTPUT_VARIABLE unpack_stdout
    ERROR_VARIABLE unpack_stderr
)
if(NOT unpack_result EQUAL 0)
    message(FATAL_ERROR "CLI unpack failed (${unpack_result}): ${unpack_stdout}${unpack_stderr}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${WORK_DIR}/input.txt" "${WORK_DIR}/restored.txt"
    RESULT_VARIABLE compare_result
)
if(NOT compare_result EQUAL 0)
    message(FATAL_ERROR "CLI restored output does not match input")
endif()

execute_process(
    COMMAND "${CLI}" unpack "${WORK_DIR}/output.txx" "${WORK_DIR}/wrong.txt" --password-stdin
    INPUT_FILE "${WORK_DIR}/wrong-password.txt"
    RESULT_VARIABLE wrong_result
    OUTPUT_VARIABLE wrong_stdout
    ERROR_VARIABLE wrong_stderr
)
if(wrong_result EQUAL 0)
    message(FATAL_ERROR "CLI unpack unexpectedly succeeded with wrong password")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
