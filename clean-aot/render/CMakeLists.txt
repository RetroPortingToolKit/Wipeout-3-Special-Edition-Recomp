cmake_minimum_required(VERSION 3.20)

project(psx_render_geometry
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(PSX_RENDER_ENABLE_SANITIZERS "Enable ASan and UBSan for render tests" OFF)

add_library(psx_render_geometry STATIC
    src/gpu_packet.cpp
    src/provenance.cpp
    src/assembly.cpp
    src/interpolation.cpp
    src/projection_policy.cpp
    src/replacement_manifest.cpp
)

target_include_directories(psx_render_geometry
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(psx_render_geometry PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(psx_render_geometry PRIVATE /W4 /permissive-)
else()
    target_compile_options(psx_render_geometry PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_executable(psx_render_geometry_tests
    tests/geometry_tests.cpp
)

target_link_libraries(psx_render_geometry_tests PRIVATE psx_render_geometry)

if(PSX_RENDER_ENABLE_SANITIZERS AND NOT MSVC)
    foreach(target psx_render_geometry psx_render_geometry_tests)
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined -fno-sanitize-recover=all
            -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE
            -fsanitize=address,undefined -fno-sanitize-recover=all)
    endforeach()
endif()

if(MSVC)
    target_compile_options(psx_render_geometry_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(psx_render_geometry_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

enable_testing()
add_test(NAME psx_render_geometry_tests COMMAND psx_render_geometry_tests)
