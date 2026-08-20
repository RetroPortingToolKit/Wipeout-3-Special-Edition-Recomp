cmake_minimum_required(VERSION 3.20)

project(clean_aot_machine LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(clean_aot_machine STATIC
    machine.cpp
)

target_include_directories(clean_aot_machine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
    target_compile_options(clean_aot_machine PRIVATE /W4 /permissive-)
else()
    target_compile_options(clean_aot_machine PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
    )
endif()

add_executable(clean_aot_machine_tests
    tests/machine_tests.cpp
)
target_link_libraries(clean_aot_machine_tests PRIVATE clean_aot_machine)

add_executable(clean_aot_machine_c_abi_test
    tests/c_abi_header_test.c
)
target_link_libraries(clean_aot_machine_c_abi_test PRIVATE clean_aot_machine)
# The consumer is compiled as C, while the implementation is C++.
set_target_properties(clean_aot_machine_c_abi_test PROPERTIES
    LINKER_LANGUAGE CXX
)

if(MSVC)
    target_compile_options(clean_aot_machine_tests PRIVATE /W4 /permissive-)
    target_compile_options(clean_aot_machine_c_abi_test PRIVATE /W4)
else()
    target_compile_options(clean_aot_machine_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
    )
    target_compile_options(clean_aot_machine_c_abi_test PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
    )
endif()

enable_testing()
add_test(NAME clean_aot_machine_tests COMMAND clean_aot_machine_tests)
add_test(NAME clean_aot_machine_c_abi_test
         COMMAND clean_aot_machine_c_abi_test)
