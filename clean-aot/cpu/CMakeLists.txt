cmake_minimum_required(VERSION 3.20)

project(r3000a_semantic_reference_oracle LANGUAGES CXX)

option(R3000A_BUILD_TESTS "Build the reference-only semantic oracle tests" ON)
option(R3000A_ENABLE_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)

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
        )
        target_link_options(r3000a_semantic_reference_tests PRIVATE
            -fsanitize=address,undefined
        )
    endif()
    add_test(NAME r3000a_semantic_reference_tests
        COMMAND r3000a_semantic_reference_tests
    )
endif()
