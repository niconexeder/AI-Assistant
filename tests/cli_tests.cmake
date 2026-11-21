if(NOT DEFINED BA_RUN OR NOT EXISTS "${BA_RUN}")
    message(FATAL_ERROR "BA_RUN does not point to the built executable: '${BA_RUN}'")
endif()
if(NOT EXISTS "${BA_DATA}/catalog.json")
    message(FATAL_ERROR "Catalog not found: '${BA_DATA}/catalog.json'")
endif()

set(catalog_path "${BA_DATA}/catalog.json")
set(work "${CMAKE_CURRENT_BINARY_DIR}")
file(WRITE "${work}/ba-cli-empty.json" "")
file(WRITE "${work}/ba-cli-invalid.json" [=[{oops]=])
file(WRITE "${work}/ba-cli-bad-role.json" [=[{"messages":[{"role":"assistant","text":"Привет"}]}]=])
file(WRITE "${work}/ba-cli-no-match.json" [=[{"messages":[{"role":"user","text":"Стол, кресло и лампа. Таганрог. До 100 рублей."}]}]=])

function(run_cli expected_code input_file out_var err_var)
    execute_process(
        COMMAND "${BA_RUN}" --catalog "${catalog_path}" --no-llm ${ARGN}
        INPUT_FILE "${input_file}"
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err
        RESULT_VARIABLE code
    )
    if(NOT code EQUAL expected_code)
        message(FATAL_ERROR
            "run ${ARGN} exited with ${code}, expected ${expected_code}\nstdout:\n${out}\nstderr:\n${err}")
    endif()
    set(${out_var} "${out}" PARENT_SCOPE)
    set(${err_var} "${err}" PARENT_SCOPE)
endfunction()

# --- Exit codes ---
execute_process(
    COMMAND "${BA_RUN}"
    INPUT_FILE "${work}/ba-cli-empty.json"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE code
)
if(NOT code EQUAL 2)
    message(FATAL_ERROR "no arguments should exit with 2, got ${code}: ${err}")
endif()

execute_process(
    COMMAND "${BA_RUN}" --catalog "${BA_DATA}/missing-catalog.json"
    INPUT_FILE "${work}/ba-cli-empty.json"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE code
)
if(NOT code EQUAL 3)
    message(FATAL_ERROR "missing catalog should exit with 3, got ${code}: ${err}")
endif()

execute_process(
    COMMAND "${BA_RUN}" --catalog "${catalog_path}" --unknown-argument
    INPUT_FILE "${work}/ba-cli-empty.json"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE code
)
if(NOT code EQUAL 2)
    message(FATAL_ERROR "unknown argument should exit with 2, got ${code}: ${err}")
endif()

execute_process(
    COMMAND "${BA_RUN}" --version
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE code
)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "--version should exit with 0, got ${code}: ${err}")
endif()
if(NOT out MATCHES "buyer-assistant")
    message(FATAL_ERROR "--version output is unexpected: ${out}")
endif()

execute_process(
    COMMAND "${BA_RUN}" --help
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE code
)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "--help should exit with 0, got ${code}: ${err}")
endif()
if(NOT out MATCHES "--catalog PATH")
    message(FATAL_ERROR "--help output is unexpected: ${out}")
endif()

run_cli(3 "${work}/ba-cli-invalid.json" out err)
run_cli(3 "${work}/ba-cli-bad-role.json" out err)

run_cli(0 "${work}/ba-cli-no-match.json" out err)
string(JSON status ERROR_VARIABLE json_error GET "${out}" status)
if(json_error)
    message(FATAL_ERROR "no_match output is not JSON: ${json_error}\n${out}")
endif()
if(NOT status STREQUAL "no_match")
    message(FATAL_ERROR "expected no_match, got '${status}'\n${out}")
endif()
string(JSON keys LENGTH "${out}")
if(NOT keys EQUAL 2)
    message(FATAL_ERROR "no_match response must contain exactly status and message: ${out}")
endif()
string(JSON has_options ERROR_VARIABLE options_error GET "${out}" options)
if(NOT options_error)
    message(FATAL_ERROR "no_match response must not contain listing identifiers: ${out}")
endif()

# --- Public example 01 ---
set(example1 "${BA_EXAMPLES}/01-selection/request.json")
run_cli(0 "${example1}" out err)
string(JSON status ERROR_VARIABLE json_error GET "${out}" status)
if(json_error)
    message(FATAL_ERROR "example 01 output is not JSON: ${json_error}\n${out}")
endif()
if(NOT status STREQUAL "ok")
    message(FATAL_ERROR "example 01 expected ok, got '${status}'\n${out}")
endif()
string(JSON option_count LENGTH "${out}" options)
if(option_count LESS 1 OR option_count GREATER 3)
    message(FATAL_ERROR "example 01 returned ${option_count} options\n${out}")
endif()

file(READ "${catalog_path}" catalog_json)
string(JSON catalog_count LENGTH "${catalog_json}")
math(EXPR last_catalog_index "${catalog_count} - 1")
foreach(index RANGE 0 ${last_catalog_index})
    string(JSON id GET "${catalog_json}" ${index} id)
    string(JSON price GET "${catalog_json}" ${index} price_rub)
    string(JSON city GET "${catalog_json}" ${index} city_id)
    string(JSON category GET "${catalog_json}" ${index} category)
    set("price_${id}" "${price}")
    set("city_${id}" "${city}")
    set("category_${id}" "${category}")
    string(JSON has_width ERROR_VARIABLE width_error GET "${catalog_json}" ${index} attributes width_cm)
    if(NOT width_error)
        set("width_${id}" "${has_width}")
    endif()
endforeach()

math(EXPR last_option "${option_count} - 1")
foreach(index RANGE 0 ${last_option})
    string(JSON ids LENGTH "${out}" options ${index} listing_ids)
    if(NOT ids EQUAL 3)
        message(FATAL_ERROR "option ${index} must contain 3 listings\n${out}")
    endif()
    string(JSON total GET "${out}" options ${index} total_rub)
    if(total GREATER 15000)
        message(FATAL_ERROR "option ${index} exceeds the budget: ${total}\n${out}")
    endif()
    set(sum 0)
    set(categories "")
    math(EXPR last_id "${ids} - 1")
    foreach(id_index RANGE 0 ${last_id})
        string(JSON id GET "${out}" options ${index} listing_ids ${id_index})
        if(NOT DEFINED price_${id})
            message(FATAL_ERROR "option ${index} refers to unknown listing '${id}'\n${out}")
        endif()
        math(EXPR sum "${sum} + ${price_${id}}")
        if(NOT city_${id} STREQUAL "taganrog")
            message(FATAL_ERROR "option ${index} uses listing '${id}' from another city\n${out}")
        endif()
        list(FIND categories "${category_${id}}" duplicate)
        if(NOT duplicate EQUAL -1)
            message(FATAL_ERROR "option ${index} repeats category ${category_${id}}\n${out}")
        endif()
        list(APPEND categories "${category_${id}}")
        if(category_${id} STREQUAL "table")
            if(NOT DEFINED width_${id})
                message(FATAL_ERROR "table '${id}' has no width attribute\n${out}")
            endif()
            if(width_${id} GREATER 100)
                message(FATAL_ERROR "table '${id}' is wider than 100 cm\n${out}")
            endif()
        endif()
    endforeach()
    if(NOT sum EQUAL total)
        message(FATAL_ERROR
            "option ${index} total ${total} does not match the sum of prices ${sum}\n${out}")
    endif()
endforeach()

# --- Public example 02 ---
set(example2 "${BA_EXAMPLES}/02-clarification/request.json")
run_cli(0 "${example2}" out err)
string(JSON status ERROR_VARIABLE json_error GET "${out}" status)
if(status STREQUAL "")
    message(FATAL_ERROR "example 02 output is not JSON: ${json_error}\n${out}")
endif()
if(NOT status STREQUAL "clarify")
    message(FATAL_ERROR "example 02 expected clarify, got '${status}'\n${out}")
endif()
string(JSON question_count LENGTH "${out}" questions)
if(NOT question_count EQUAL 1)
    message(FATAL_ERROR "example 02 must ask exactly one question\n${out}")
endif()
string(JSON field GET "${out}" questions 0 field)
if(NOT field STREQUAL "budget_rub")
    message(FATAL_ERROR "example 02 must ask for budget_rub, got '${field}'\n${out}")
endif()

# --- Requirements dumping goes to stderr, stdout stays machine-readable ---
execute_process(
    COMMAND "${BA_RUN}" --catalog "${catalog_path}" --no-llm --dump-requirements
    INPUT_FILE "${example1}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE code
)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "--dump-requirements exited with ${code}: ${err}")
endif()
if(NOT err MATCHES "requirements:")
    message(FATAL_ERROR "--dump-requirements did not write to stderr: ${err}")
endif()
string(JSON status ERROR_VARIABLE json_error GET "${out}" status)
if(NOT status STREQUAL "ok")
    message(FATAL_ERROR "--dump-requirements corrupted stdout: ${out}")
endif()
