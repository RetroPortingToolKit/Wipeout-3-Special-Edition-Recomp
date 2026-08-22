cmake_minimum_required(VERSION 3.20)

project(clean_aot_memory LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CLEAN_AOT_MEMORY_ENABLE_SANITIZERS
       "Build the memory tests with AddressSanitizer and UBSan"
       OFF)
option(CLEAN_AOT_MEMORY_WARNINGS_AS_ERRORS
       "Treat compiler warnings as errors"
       ON)

add_library(clean_aot_memory STATIC
    src/bus.cpp
)

target_include_directories(clean_aot_memory
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(clean_aot_memory_tests
    tests/memory_tests.cpp
)
target_link_libraries(clean_aot_memory_tests PRIVATE clean_aot_memory)

foreach(target clean_aot_memory clean_aot_memory_tests)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(CLEAN_AOT_MEMORY_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
        )
        if(CLEAN_AOT_MEMORY_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
        if(CLEAN_AOT_MEMORY_ENABLE_SANITIZERS)
            target_compile_options(${target} PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer
            )
        endif()
    endif()
endforeach()

if(CLEAN_AOT_MEMORY_ENABLE_SANITIZERS AND NOT MSVC)
    target_link_options(clean_aot_memory_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()

enable_testing()
add_test(NAME clean_aot_memory_tests COMMAND clean_aot_memory_tests)
