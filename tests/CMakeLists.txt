add_executable(ba_tests
    test_main.cpp
    test_application.cpp
    test_catalog.cpp
    test_cli.cpp
    test_config.cpp
    test_decision.cpp
    test_dialogue.cpp
    test_interpreter.cpp
    test_json.cpp
    test_llm.cpp
    test_search.cpp
    test_text.cpp
)

target_link_libraries(ba_tests PRIVATE ba_core)
target_compile_definitions(ba_tests PRIVATE BA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
ba_enable_warnings(ba_tests)

add_test(NAME buyer_assistant.unit COMMAND ba_tests)
set_tests_properties(buyer_assistant.unit PROPERTIES TIMEOUT 120)

add_test(
    NAME buyer_assistant.cli
    COMMAND "${CMAKE_COMMAND}"
        "-DBA_RUN=$<TARGET_FILE:buyer_assistant>"
        "-DBA_DATA=${CMAKE_SOURCE_DIR}/data"
        "-DBA_EXAMPLES=${CMAKE_SOURCE_DIR}/examples"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cli_tests.cmake"
)
set_tests_properties(buyer_assistant.cli PROPERTIES TIMEOUT 120)
