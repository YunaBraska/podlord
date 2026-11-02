if(NOT DEFINED AUDIO_TEST OR NOT DEFINED MODE OR NOT DEFINED SOURCE OR NOT DEFINED EXPECTED_MESSAGE)
    message(FATAL_ERROR "Audio failure check requires an executable, mode, source and expected message")
endif()

execute_process(COMMAND "${AUDIO_TEST}" "${MODE}" "${SOURCE}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 5)
if(NOT "${result}" STREQUAL "1")
    message(FATAL_ERROR "Expected audio failure exit 1, got ${result}\n${output}\n${error}")
endif()
if(NOT error MATCHES "${EXPECTED_MESSAGE}")
    message(FATAL_ERROR "Audio failed for an unexpected reason\n${output}\n${error}")
endif()
message(STATUS "Expected audio failure: ${EXPECTED_MESSAGE}")
