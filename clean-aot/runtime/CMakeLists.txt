cmake_minimum_required(VERSION 3.20)

project(clean_aot_runtime LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

foreach(component IN ITEMS machine memory soc gte gpu cdrom mdec sio spu)
    string(TOUPPER "${component}" component_upper)
    if(NOT TARGET clean_aot_${component})
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../${component}"
                         "${CMAKE_CURRENT_BINARY_DIR}/${component}")
    endif()
endforeach()

add_library(clean_aot_runtime STATIC
    src/game_profile_trace.cpp
    src/native_dispatch.cpp
    src/runtime.cpp
    src/target_trace.cpp
)

target_include_directories(clean_aot_runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(clean_aot_runtime
    PUBLIC
        clean_aot_machine
        clean_aot_memory
        clean_aot_soc
        clean_aot_gte
        clean_aot_gpu
        clean_aot_cdrom
        clean_aot_mdec
        clean_aot_sio
        clean_aot_spu
)

if(MSVC)
    target_compile_options(clean_aot_runtime PRIVATE /W4 /permissive-)
else()
    target_compile_options(clean_aot_runtime PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror
    )
endif()

include(CTest)
if(BUILD_TESTING)
    if(NOT TARGET r3000a_semantic_reference_oracle)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../cpu" "${CMAKE_CURRENT_BINARY_DIR}/cpu")
    endif()
    add_executable(clean_aot_runtime_tests tests/runtime_tests.cpp)
    target_link_libraries(clean_aot_runtime_tests PRIVATE clean_aot_runtime r3000a_semantic_reference_oracle)
    if(MSVC)
        target_compile_options(clean_aot_runtime_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(clean_aot_runtime_tests PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror
        )
    endif()
    add_test(NAME clean_aot_runtime_tests COMMAND clean_aot_runtime_tests)
    add_executable(clean_aot_game_transition_tests tests/game_transition_tests.cpp)
    target_link_libraries(clean_aot_game_transition_tests PRIVATE clean_aot_runtime)
    target_compile_features(clean_aot_game_transition_tests PRIVATE cxx_std_20)
    add_test(NAME clean_aot_game_transition_tests COMMAND clean_aot_game_transition_tests)
    add_executable(clean_aot_runtime_cfi_tests tests/cfi_tests.cpp)
    target_link_libraries(clean_aot_runtime_cfi_tests PRIVATE clean_aot_runtime)
    target_compile_features(clean_aot_runtime_cfi_tests PRIVATE cxx_std_20)
    add_test(NAME clean_aot_runtime_cfi_tests COMMAND clean_aot_runtime_cfi_tests)
    add_executable(clean_aot_runtime_target_trace_tests tests/target_trace_tests.cpp)
    target_link_libraries(clean_aot_runtime_target_trace_tests PRIVATE clean_aot_runtime)
    target_compile_features(clean_aot_runtime_target_trace_tests PRIVATE cxx_std_20)
    add_test(NAME clean_aot_runtime_target_trace_tests
             COMMAND clean_aot_runtime_target_trace_tests)
    add_executable(clean_aot_runtime_game_profile_trace_tests
        tests/game_profile_trace_tests.cpp)
    target_link_libraries(clean_aot_runtime_game_profile_trace_tests PRIVATE
        clean_aot_runtime)
    target_compile_features(clean_aot_runtime_game_profile_trace_tests PRIVATE cxx_std_20)
    add_test(NAME clean_aot_runtime_game_profile_trace_tests
             COMMAND clean_aot_runtime_game_profile_trace_tests)
    add_executable(clean_aot_runtime_packed_dispatch_tests
        tests/packed_dispatch_tests.cpp)
    target_link_libraries(clean_aot_runtime_packed_dispatch_tests PRIVATE clean_aot_runtime)
    target_compile_features(clean_aot_runtime_packed_dispatch_tests PRIVATE cxx_std_20)
    add_test(NAME clean_aot_runtime_packed_dispatch_tests
             COMMAND clean_aot_runtime_packed_dispatch_tests)
    add_executable(clean_aot_runtime_dispatch_benchmark
        tests/dispatch_benchmark.cpp)
    target_link_libraries(clean_aot_runtime_dispatch_benchmark PRIVATE clean_aot_runtime)
    target_compile_features(clean_aot_runtime_dispatch_benchmark PRIVATE cxx_std_20)
    add_executable(clean_aot_cdrom_bios_sequence_tests
        tests/cdrom_bios_sequence_tests.cpp)
    target_link_libraries(clean_aot_cdrom_bios_sequence_tests PRIVATE
        clean_aot_runtime)
    target_compile_features(clean_aot_cdrom_bios_sequence_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(clean_aot_cdrom_bios_sequence_tests PRIVATE
            /W4 /permissive-)
    else()
        target_compile_options(clean_aot_cdrom_bios_sequence_tests PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
    endif()
    add_test(NAME clean_aot_cdrom_bios_sequence_tests
        COMMAND clean_aot_cdrom_bios_sequence_tests)
endif()
