cmake_minimum_required(VERSION 3.20)

project(w3_clean_machine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(w3_clean_machine STATIC
    machine.cpp
)

add_library(w3_clean_machine_c_header_test OBJECT
    tests/c_abi_header_test.c
)
target_include_directories(w3_clean_machine_c_header_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(w3_clean_machine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
    target_compile_options(w3_clean_machine PRIVATE /W4 /permissive-)
else()
    target_compile_options(w3_clean_machine PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
    )
endif()

add_executable(w3_clean_machine_tests
    tests/machine_tests.cpp
)
target_link_libraries(w3_clean_machine_tests PRIVATE w3_clean_machine)

if(MSVC)
    target_compile_options(w3_clean_machine_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(w3_clean_machine_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
    )
endif()

enable_testing()
add_test(NAME w3_clean_machine_tests COMMAND w3_clean_machine_tests)
