cmake_minimum_required(VERSION 3.20)

project(clean_aot_compiler LANGUAGES CXX)

include(CTest)

add_library(clean_aot_compiler STATIC
    src/cpp_emitter.cpp
    src/ir.cpp
    src/mips_decoder.cpp
    src/psx_exe.cpp
    src/region_builder.cpp
)

target_compile_features(clean_aot_compiler PUBLIC cxx_std_20)
target_include_directories(clean_aot_compiler
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(clean_aot_compiler PRIVATE /W4 /permissive-)
else()
    target_compile_options(clean_aot_compiler PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
endif()

add_executable(clean_aot_compiler_tests
    tests/compiler_tests.cpp
)
target_compile_features(clean_aot_compiler_tests PRIVATE cxx_std_20)
target_link_libraries(clean_aot_compiler_tests PRIVATE clean_aot_compiler)

if(MSVC)
    target_compile_options(clean_aot_compiler_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(clean_aot_compiler_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
endif()

if(BUILD_TESTING)
    add_test(NAME clean_aot_compiler_tests COMMAND clean_aot_compiler_tests)
endif()
