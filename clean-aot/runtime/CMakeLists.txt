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
    src/native_dispatch.cpp
    src/runtime.cpp
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
    add_executable(clean_aot_runtime_tests tests/runtime_tests.cpp)
    target_link_libraries(clean_aot_runtime_tests PRIVATE clean_aot_runtime)
    if(MSVC)
        target_compile_options(clean_aot_runtime_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(clean_aot_runtime_tests PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror
        )
    endif()
    add_test(NAME clean_aot_runtime_tests COMMAND clean_aot_runtime_tests)
endif()
