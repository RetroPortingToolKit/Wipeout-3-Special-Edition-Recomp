cmake_minimum_required(VERSION 3.20)

project(clean_aot_host LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CLEAN_AOT_HOST_BUILD_TESTS
       "Build the display-free host contract tests"
       ON)
option(CLEAN_AOT_HOST_ENABLE_SANITIZERS
       "Enable AddressSanitizer and UndefinedBehaviorSanitizer"
       OFF)
option(CLEAN_AOT_HOST_WARNINGS_AS_ERRORS
       "Treat host warnings as errors"
       ON)
option(CLEAN_AOT_HOST_ENABLE_SDL_VULKAN
       "Build the optional Windows SDL3/Vulkan presentation backend"
       OFF)
option(CLEAN_AOT_HOST_REBUILD_SHADERS
       "Compile the presentation shaders with the pinned DXC release"
       OFF)

if(NOT TARGET psx_render_geometry)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../render"
                     "${CMAKE_CURRENT_BINARY_DIR}/render")
endif()
if(NOT TARGET clean_aot_gpu)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../gpu"
                     "${CMAKE_CURRENT_BINARY_DIR}/gpu")
endif()
if(NOT TARGET clean_aot_raster)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../raster"
                     "${CMAKE_CURRENT_BINARY_DIR}/raster")
endif()

add_library(clean_aot_host STATIC
    src/headless.cpp
    src/pacing.cpp
    src/viewport.cpp
    src/render_interfaces.cpp
)

target_compile_features(clean_aot_host PUBLIC cxx_std_20)
target_include_directories(clean_aot_host
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../gpu/include
)
target_link_libraries(clean_aot_host PUBLIC psx_render_geometry clean_aot_gpu clean_aot_raster)

if(MSVC)
    target_compile_options(clean_aot_host PRIVATE /W4 /permissive-)
    if(CLEAN_AOT_HOST_WARNINGS_AS_ERRORS)
        target_compile_options(clean_aot_host PRIVATE /WX)
    endif()
else()
    target_compile_options(clean_aot_host PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
    )
    if(CLEAN_AOT_HOST_WARNINGS_AS_ERRORS)
        target_compile_options(clean_aot_host PRIVATE -Werror)
    endif()
endif()

if(CLEAN_AOT_HOST_ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(clean_aot_host PRIVATE
        -fsanitize=address,undefined -fno-sanitize-recover=all
        -fno-omit-frame-pointer
    )
    target_link_options(clean_aot_host PRIVATE
        -fsanitize=address,undefined -fno-sanitize-recover=all
    )
endif()

set(CLEAN_AOT_HOST_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(CLEAN_AOT_HOST_SHADER_HEADER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/present_shaders.hpp")

if(CLEAN_AOT_HOST_REBUILD_SHADERS)
    file(GLOB CLEAN_AOT_HOST_USER_DXC_CANDIDATES
        "$ENV{LOCALAPPDATA}/Microsoft/WinGet/Packages/Microsoft.DirectX.ShaderCompiler_*/bin/x64/dxc.exe"
    )
    find_program(CLEAN_AOT_HOST_DXC_EXECUTABLE
        NAMES dxc dxc.exe
        HINTS
            "$ENV{DXC_ROOT}"
            "$ENV{LOCALAPPDATA}/Microsoft/WinGet/Packages"
            PATH_SUFFIXES bin/x64 bin
    )
    if(NOT CLEAN_AOT_HOST_DXC_EXECUTABLE AND
       CLEAN_AOT_HOST_USER_DXC_CANDIDATES)
        list(SORT CLEAN_AOT_HOST_USER_DXC_CANDIDATES)
        list(GET CLEAN_AOT_HOST_USER_DXC_CANDIDATES 0
            CLEAN_AOT_HOST_DXC_EXECUTABLE)
    endif()
    if(NOT CLEAN_AOT_HOST_DXC_EXECUTABLE)
        message(FATAL_ERROR
            "CLEAN_AOT_HOST_REBUILD_SHADERS requires the official DXC 1.9.2602.17 executable")
    endif()

    set(CLEAN_AOT_HOST_SHADER_HEADER_BINARY
        "${CMAKE_CURRENT_BINARY_DIR}/generated/present_shaders.hpp")
    add_custom_command(
        OUTPUT "${CLEAN_AOT_HOST_SHADER_HEADER_BINARY}"
        COMMAND "${CMAKE_COMMAND}"
            "-DDXC_EXECUTABLE=${CLEAN_AOT_HOST_DXC_EXECUTABLE}"
            "-DSHADER_SOURCE_DIR=${CLEAN_AOT_HOST_SHADER_SOURCE_DIR}"
            "-DOUTPUT_HEADER=${CLEAN_AOT_HOST_SHADER_HEADER_BINARY}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/compile_shaders.cmake"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/compile_shaders.cmake"
            "${CLEAN_AOT_HOST_SHADER_SOURCE_DIR}/present.vert.hlsl"
            "${CLEAN_AOT_HOST_SHADER_SOURCE_DIR}/present.frag.hlsl"
        VERBATIM
    )
    add_custom_target(clean_aot_host_shaders
        DEPENDS "${CLEAN_AOT_HOST_SHADER_HEADER_BINARY}")
    add_dependencies(clean_aot_host clean_aot_host_shaders)
    set(CLEAN_AOT_HOST_SHADER_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
else()
    set(CLEAN_AOT_HOST_SHADER_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated")
endif()

target_include_directories(clean_aot_host
    PUBLIC
        "${CLEAN_AOT_HOST_SHADER_INCLUDE_DIR}"
)

if(CLEAN_AOT_HOST_BUILD_TESTS)
    add_executable(clean_aot_host_tests
        tests/host_tests.cpp
    )
    target_link_libraries(clean_aot_host_tests PRIVATE clean_aot_host)
    target_compile_features(clean_aot_host_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(clean_aot_host_tests PRIVATE /W4 /permissive-)
        if(CLEAN_AOT_HOST_WARNINGS_AS_ERRORS)
            target_compile_options(clean_aot_host_tests PRIVATE /WX)
        endif()
    else()
        target_compile_options(clean_aot_host_tests PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
        )
        if(CLEAN_AOT_HOST_WARNINGS_AS_ERRORS)
            target_compile_options(clean_aot_host_tests PRIVATE -Werror)
        endif()
    endif()
    if(CLEAN_AOT_HOST_ENABLE_SANITIZERS AND NOT MSVC)
        target_compile_options(clean_aot_host_tests PRIVATE
            -fsanitize=address,undefined -fno-sanitize-recover=all
            -fno-omit-frame-pointer
        )
        target_link_options(clean_aot_host_tests PRIVATE
            -fsanitize=address,undefined -fno-sanitize-recover=all
        )
    endif()
    add_test(NAME clean_aot_host_tests COMMAND clean_aot_host_tests)
endif()

if(CLEAN_AOT_HOST_ENABLE_SDL_VULKAN)
    if(NOT WIN32)
        message(FATAL_ERROR
            "The SDL3/Vulkan presentation tranche is intentionally Windows-only")
    endif()

    include(FetchContent)
    set(SDL_SHARED ON CACHE BOOL "Build SDL3 as a shared library" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "Do not build a second SDL3 static library" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "Do not build SDL3 tests" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "Do not build SDL3 test library" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "Do not install the fetched SDL3 dependency" FORCE)
    FetchContent_Declare(SDL3
        URL
            https://github.com/libsdl-org/SDL/releases/download/release-3.4.10/SDL3-3.4.10.tar.gz
        URL_HASH
            SHA256=12b34280415ec8418c864408b93d008a20a6530687ee613d60bfbd20411f2785
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(SDL3)

    add_library(clean_aot_host_sdl_vulkan STATIC
        src/sdl_vulkan_backend_win32.cpp
    )
    target_compile_features(clean_aot_host_sdl_vulkan PUBLIC cxx_std_20)
    target_compile_definitions(clean_aot_host_sdl_vulkan PRIVATE
        CLEAN_AOT_HOST_SDL_VULKAN=1
    )
    target_include_directories(clean_aot_host_sdl_vulkan
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            "${CLEAN_AOT_HOST_SHADER_INCLUDE_DIR}"
    )
    target_link_libraries(clean_aot_host_sdl_vulkan
        PUBLIC
            clean_aot_host
            SDL3::SDL3
    )
    if(CLEAN_AOT_HOST_REBUILD_SHADERS)
        add_dependencies(clean_aot_host_sdl_vulkan clean_aot_host_shaders)
    endif()
    if(MSVC)
        target_compile_options(clean_aot_host_sdl_vulkan PRIVATE /W4 /permissive-)
        if(CLEAN_AOT_HOST_WARNINGS_AS_ERRORS)
            target_compile_options(clean_aot_host_sdl_vulkan PRIVATE /WX)
        endif()
    else()
        target_compile_options(clean_aot_host_sdl_vulkan PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
        )
        if(CLEAN_AOT_HOST_WARNINGS_AS_ERRORS)
            target_compile_options(clean_aot_host_sdl_vulkan PRIVATE -Werror)
        endif()
    endif()
endif()
