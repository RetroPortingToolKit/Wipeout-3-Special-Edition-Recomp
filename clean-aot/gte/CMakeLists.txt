cmake_minimum_required(VERSION 3.20)

project(clean_aot_gte LANGUAGES CXX)

include(CTest)

add_library(clean_aot_gte STATIC
    gte.cpp
)

target_compile_features(clean_aot_gte PUBLIC cxx_std_20)
target_include_directories(clean_aot_gte
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(clean_aot_gte_tests
    tests/gte_tests.cpp
)
target_compile_features(clean_aot_gte_tests PRIVATE cxx_std_20)
target_link_libraries(clean_aot_gte_tests PRIVATE clean_aot_gte)

if(MSVC)
    target_compile_options(clean_aot_gte PRIVATE /W4 /WX /permissive-)
    target_compile_options(clean_aot_gte_tests PRIVATE /W4 /WX /permissive-)
else()
    set(CLEAN_AOT_GTE_STRICT_WARNINGS
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Werror
    )
    target_compile_options(clean_aot_gte PRIVATE ${CLEAN_AOT_GTE_STRICT_WARNINGS})
    target_compile_options(clean_aot_gte_tests PRIVATE ${CLEAN_AOT_GTE_STRICT_WARNINGS})
endif()

if(BUILD_TESTING)
    add_test(NAME clean_aot_gte_tests COMMAND clean_aot_gte_tests)
endif()
