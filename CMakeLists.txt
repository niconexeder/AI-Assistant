cmake_minimum_required(VERSION 3.20)

project(buyer_assistant
    VERSION 1.0.0
    DESCRIPTION "C++ buyer assistant for the SFEDU hackathon case"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BA_BUILD_TESTS "Build the buyer assistant test suite" ON)

add_library(ba_core STATIC
    src/app/Application.cpp
    src/cli/Arguments.cpp
    src/core/BundleSearch.cpp
    src/core/Catalog.cpp
    src/core/DecisionEngine.cpp
    src/core/DialogueAnalyzer.cpp
    src/core/Directories.cpp
    src/core/MessageInterpreter.cpp
    src/models/Directory.cpp
    src/models/Requirements.cpp
    src/models/Response.cpp
    src/services/Config.cpp
    src/services/LlmClient.cpp
    src/services/LlmExtractor.cpp
    src/utils/JsonIO.cpp
    src/utils/RussianText.cpp
)

target_include_directories(ba_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)

if(WIN32)
    target_sources(ba_core PRIVATE src/services/WinHttpTransport.cpp)
    target_link_libraries(ba_core PUBLIC winhttp)
else()
    target_sources(ba_core PRIVATE src/services/CurlHttpTransport.cpp)
    find_package(CURL REQUIRED)
    target_link_libraries(ba_core PUBLIC CURL::libcurl)
endif()

function(ba_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /permissive- /utf-8 /EHsc /Zc:__cplusplus
        )
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        )
    endif()
endfunction()

ba_enable_warnings(ba_core)

add_executable(buyer_assistant src/main.cpp)
set_target_properties(buyer_assistant PROPERTIES OUTPUT_NAME run)
target_link_libraries(buyer_assistant PRIVATE ba_core)
ba_enable_warnings(buyer_assistant)

if(WIN32)
    target_link_libraries(buyer_assistant PRIVATE shell32)
endif()

add_custom_command(TARGET buyer_assistant POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/data" "$<TARGET_FILE_DIR:buyer_assistant>/data"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/examples" "$<TARGET_FILE_DIR:buyer_assistant>/examples"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/schemas" "$<TARGET_FILE_DIR:buyer_assistant>/schemas"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/docs" "$<TARGET_FILE_DIR:buyer_assistant>/docs"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config.example.json"
        "$<TARGET_FILE_DIR:buyer_assistant>/config.example.json"
    COMMENT "Copying data and documentation next to the executable"
)

include(CTest)
if(BA_BUILD_TESTS)
    add_subdirectory(tests)
endif()

install(TARGETS buyer_assistant RUNTIME DESTINATION .)
install(DIRECTORY data examples schemas docs DESTINATION .)
install(FILES config.example.json DESTINATION .)
