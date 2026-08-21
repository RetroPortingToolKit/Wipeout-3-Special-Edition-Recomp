cmake_minimum_required(VERSION 3.20)

project(clean_aot_soc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CLEAN_AOT_SOC_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(clean_aot_soc STATIC
    src/clean_aot_soc.cpp
)

target_include_directories(clean_aot_soc
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(clean_aot_soc PUBLIC cxx_std_20)

add_executable(clean_aot_soc_tests
    tests/soc_tests.cpp
)
target_link_libraries(clean_aot_soc_tests PRIVATE clean_aot_soc)

if(MSVC)
    target_compile_options(clean_aot_soc PRIVATE /W4 /WX /permissive-)
    target_compile_options(clean_aot_soc_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(clean_aot_soc PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror
    )
    target_compile_options(clean_aot_soc_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror
    )
endif()

if(CLEAN_AOT_SOC_SANITIZERS AND
   CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(clean_aot_soc PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(clean_aot_soc PRIVATE -fsanitize=address,undefined)
    target_compile_options(clean_aot_soc_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(clean_aot_soc_tests PRIVATE -fsanitize=address,undefined)
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME clean_aot_soc_tests COMMAND clean_aot_soc_tests)

    find_program(CLEAN_AOT_SOC_PYTHON_EXECUTABLE NAMES python python3)
    if(CLEAN_AOT_SOC_PYTHON_EXECUTABLE)
        add_test(
            NAME clean_aot_soc_cleanroom_scan
            COMMAND ${CLEAN_AOT_SOC_PYTHON_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/cleanroom_scan.py
                    ${CMAKE_CURRENT_SOURCE_DIR}
        )
    endif()
endif()
