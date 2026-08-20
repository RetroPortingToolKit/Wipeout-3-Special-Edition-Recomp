cmake_minimum_required(VERSION 3.20)

set(_r3000a_is_standalone OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(r3000a_semantic_reference_oracle LANGUAGES CXX)
    set(_r3000a_is_standalone ON)
endif()

set(_r3000a_default_build OFF)
if(_r3000a_is_standalone OR BUILD_TESTING)
    set(_r3000a_default_build ON)
endif()

option(R3000A_BUILD_REFERENCE_ORACLE
    "Build the offline R3000A semantic reference target"
    ${_r3000a_default_build}
)
option(R3000A_BUILD_TESTS
    "Build the reference-only semantic oracle tests"
    ${_r3000a_default_build}
)
option(R3000A_ENABLE_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)

if(R3000A_BUILD_TESTS AND NOT R3000A_BUILD_REFERENCE_ORACLE)
    message(FATAL_ERROR "R3000A_BUILD_TESTS requires R3000A_BUILD_REFERENCE_ORACLE")
endif()

# A parent release build gets no CPU target unless it explicitly requests this
# build/test-only oracle or enables its tests.
if(NOT R3000A_BUILD_REFERENCE_ORACLE)
    return()
endif()

add_library(r3000a_semantic_reference_oracle STATIC
    src/reference.cpp
)

target_compile_features(r3000a_semantic_reference_oracle PUBLIC cxx_std_20)
target_include_directories(r3000a_semantic_reference_oracle
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_definitions(r3000a_semantic_reference_oracle
    PUBLIC R3000A_SEMANTIC_REFERENCE_ONLY=1
)
set_target_properties(r3000a_semantic_reference_oracle PROPERTIES
    CXX_EXTENSIONS OFF
    R3000A_REFERENCE_ONLY TRUE
)

if(MSVC)
    target_compile_options(r3000a_semantic_reference_oracle PRIVATE /W4 /WX)
else()
    target_compile_options(r3000a_semantic_reference_oracle PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror
    )
endif()

if(R3000A_ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(r3000a_semantic_reference_oracle PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer
        -fno-sanitize-recover=all
    )
    target_link_options(r3000a_semantic_reference_oracle PRIVATE
        -fsanitize=address,undefined
    )
endif()

if(R3000A_BUILD_TESTS)
    enable_testing()
    add_executable(r3000a_semantic_reference_tests
        tests/test_reference.cpp
    )
    target_compile_features(r3000a_semantic_reference_tests PRIVATE cxx_std_20)
    target_link_libraries(r3000a_semantic_reference_tests PRIVATE
        r3000a_semantic_reference_oracle
    )
    set_target_properties(r3000a_semantic_reference_tests PROPERTIES
        CXX_EXTENSIONS OFF
        R3000A_REFERENCE_ONLY TRUE
    )
    if(MSVC)
        target_compile_options(r3000a_semantic_reference_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(r3000a_semantic_reference_tests PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror
        )
    endif()
    if(R3000A_ENABLE_SANITIZERS AND NOT MSVC)
        target_compile_options(r3000a_semantic_reference_tests PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer
            -fno-sanitize-recover=all
        )
        target_link_options(r3000a_semantic_reference_tests PRIVATE
            -fsanitize=address,undefined
        )
    endif()
    add_test(NAME r3000a_semantic_reference_tests
        COMMAND r3000a_semantic_reference_tests
    )
endif()
