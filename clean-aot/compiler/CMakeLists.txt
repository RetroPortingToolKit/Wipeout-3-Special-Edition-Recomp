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

add_executable(clean_aot_compiler_cli src/compiler_cli.cpp)
target_compile_features(clean_aot_compiler_cli PRIVATE cxx_std_20)
target_link_libraries(clean_aot_compiler_cli PRIVATE clean_aot_compiler)
if(MSVC)
    target_compile_options(clean_aot_compiler_cli PRIVATE /W4 /permissive-)
else()
    target_compile_options(clean_aot_compiler_cli PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()
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

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    set(CLEAN_AOT_COMPILER_GENERATED_FIXTURE
        ${CMAKE_CURRENT_BINARY_DIR}/generated_compiler_fixture.cpp
    )
    add_custom_command(
        OUTPUT ${CLEAN_AOT_COMPILER_GENERATED_FIXTURE}
        COMMAND $<TARGET_FILE:clean_aot_compiler_tests>
                --emit ${CLEAN_AOT_COMPILER_GENERATED_FIXTURE}
        DEPENDS clean_aot_compiler_tests
        COMMENT "Generating deterministic clean-AOT compiler fixture"
        VERBATIM
    )
    add_library(clean_aot_compiler_generated_fixture OBJECT
        ${CLEAN_AOT_COMPILER_GENERATED_FIXTURE}
    )
    add_executable(clean_aot_generated_fixture_tests
        tests/generated_fixture_tests.cpp
        $<TARGET_OBJECTS:clean_aot_compiler_generated_fixture>)
    target_include_directories(clean_aot_generated_fixture_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/include)
    target_compile_features(clean_aot_generated_fixture_tests PRIVATE cxx_std_20)
    add_test(NAME clean_aot_generated_fixture_tests COMMAND clean_aot_generated_fixture_tests)
    target_compile_features(clean_aot_compiler_generated_fixture PRIVATE cxx_std_20)
    target_include_directories(clean_aot_compiler_generated_fixture PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/include)
    if(MSVC)
        target_compile_options(clean_aot_compiler_generated_fixture PRIVATE
            /W4
            /permissive-
        )
    else()
        target_compile_options(clean_aot_compiler_generated_fixture PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
        )
    endif()
endif()

if(BUILD_TESTING)
    add_test(NAME clean_aot_compiler_tests COMMAND clean_aot_compiler_tests)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME clean_aot_image_adapter_integration
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/product/tests/test_clean_aot_image_adapter.py
            $<TARGET_FILE:clean_aot_compiler_cli>)
endif()
