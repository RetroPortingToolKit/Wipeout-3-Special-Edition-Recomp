cmake_minimum_required(VERSION 3.20)

project(clean_aot_spu LANGUAGES CXX)

if(NOT TARGET clean_aot_cdrom)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../cdrom ${CMAKE_CURRENT_BINARY_DIR}/cdrom)
endif()
if(NOT TARGET clean_aot_media)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../media ${CMAKE_CURRENT_BINARY_DIR}/media)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CLEAN_AOT_SPU_WARNINGS_AS_ERRORS
       "Treat SPU compiler warnings as errors"
       ON)
option(CLEAN_AOT_SPU_ENABLE_SANITIZERS
       "Build SPU targets with AddressSanitizer and UBSan"
       OFF)

add_library(clean_aot_spu STATIC
    src/spu.cpp
    src/cd_audio.cpp
)

target_include_directories(clean_aot_spu
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(clean_aot_spu PUBLIC clean_aot_cdrom clean_aot_media)

add_executable(clean_aot_spu_tests
    tests/spu_tests.cpp
)
target_link_libraries(clean_aot_spu_tests PRIVATE clean_aot_spu)

add_executable(clean_aot_spu_benchmark
    tests/spu_benchmark.cpp
)
target_link_libraries(clean_aot_spu_benchmark PRIVATE clean_aot_spu)

add_executable(clean_aot_cd_audio_tests tests/cd_audio_tests.cpp)
target_link_libraries(clean_aot_cd_audio_tests PRIVATE clean_aot_spu)

set(_clean_aot_spu_targets
    clean_aot_spu
    clean_aot_spu_tests
    clean_aot_spu_benchmark
    clean_aot_cd_audio_tests
)
foreach(target IN LISTS _clean_aot_spu_targets)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(CLEAN_AOT_SPU_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
        )
        if(CLEAN_AOT_SPU_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
        if(CLEAN_AOT_SPU_ENABLE_SANITIZERS)
            target_compile_options(${target} PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer
                -fno-sanitize-recover=all
            )
            target_link_options(${target} PRIVATE
                -fsanitize=address,undefined -fno-sanitize-recover=all
            )
        endif()
    endif()
endforeach()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME clean_aot_spu_tests COMMAND clean_aot_spu_tests)
    add_test(NAME clean_aot_cd_audio_tests COMMAND clean_aot_cd_audio_tests)
endif()
