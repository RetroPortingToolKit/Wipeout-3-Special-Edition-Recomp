cmake_minimum_required(VERSION 3.20)

project(clean_aot_mdec LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_EXTENSIONS OFF)

add_library(clean_aot_mdec STATIC
    mdec.cpp
)

target_compile_features(clean_aot_mdec PUBLIC cxx_std_20)
target_include_directories(clean_aot_mdec
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

set(CLEAN_AOT_MDEC_STRICT_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Werror
)

if(MSVC)
    target_compile_options(clean_aot_mdec PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(clean_aot_mdec PRIVATE ${CLEAN_AOT_MDEC_STRICT_WARNINGS})
endif()

add_executable(clean_aot_mdec_tests
    tests/mdec_tests.cpp
)
target_compile_features(clean_aot_mdec_tests PRIVATE cxx_std_20)
target_link_libraries(clean_aot_mdec_tests PRIVATE clean_aot_mdec)
if(MSVC)
    target_compile_options(clean_aot_mdec_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(clean_aot_mdec_tests PRIVATE ${CLEAN_AOT_MDEC_STRICT_WARNINGS})
endif()

if(BUILD_TESTING)
    add_test(NAME clean_aot_mdec_tests COMMAND clean_aot_mdec_tests)
endif()
