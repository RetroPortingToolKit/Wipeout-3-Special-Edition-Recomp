cmake_minimum_required(VERSION 3.20)
project(clean_aot_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CLEAN_AOT_APP_BUILD_TESTS "Build application-shell tests" ON)
option(CLEAN_AOT_APP_ENABLE_SDL "Build the native SDL platform adapter" OFF)
option(CLEAN_AOT_APP_ENABLE_SANITIZERS "Enable ASan and UBSan" OFF)

include(CTest)
set(_clean_aot_app_build_testing "${BUILD_TESTING}")
set(BUILD_TESTING OFF)
set(CLEAN_AOT_HOST_BUILD_TESTS OFF CACHE BOOL "" FORCE)

if(NOT TARGET clean_aot_runtime)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../runtime"
                     "${CMAKE_CURRENT_BINARY_DIR}/runtime")
endif()
set(BUILD_TESTING "${_clean_aot_app_build_testing}")
if(NOT TARGET clean_aot_host)
    if(CLEAN_AOT_APP_ENABLE_SDL)
        set(CLEAN_AOT_HOST_ENABLE_SDL_VULKAN ON CACHE BOOL "" FORCE)
    endif()
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../host"
                     "${CMAKE_CURRENT_BINARY_DIR}/host")
endif()

add_library(clean_aot_app STATIC
    src/application.cpp
    src/authorization.cpp
    src/json.cpp
    src/sha256.cpp
)
target_include_directories(clean_aot_app PUBLIC include)
target_link_libraries(clean_aot_app PUBLIC clean_aot_runtime clean_aot_host)
target_compile_features(clean_aot_app PUBLIC cxx_std_20)

function(clean_aot_app_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
    endif()
    if(CLEAN_AOT_APP_ENABLE_SANITIZERS AND NOT MSVC)
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE
            -fsanitize=address,undefined -fno-sanitize-recover=all)
    endif()
endfunction()
clean_aot_app_warnings(clean_aot_app)

if(CLEAN_AOT_APP_ENABLE_SDL)
    add_library(clean_aot_app_sdl STATIC src/sdl_platform.cpp)
    target_include_directories(clean_aot_app_sdl PUBLIC include)
    target_link_libraries(clean_aot_app_sdl PUBLIC clean_aot_app SDL3::SDL3)
    clean_aot_app_warnings(clean_aot_app_sdl)
    if(CLEAN_AOT_APP_BUILD_TESTS AND BUILD_TESTING)
        add_executable(clean_aot_app_sdl_tests tests/sdl_platform_tests.cpp)
        target_link_libraries(clean_aot_app_sdl_tests PRIVATE clean_aot_app_sdl)
        clean_aot_app_warnings(clean_aot_app_sdl_tests)
        add_custom_command(TARGET clean_aot_app_sdl_tests POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:SDL3::SDL3>"
                "$<TARGET_FILE_DIR:clean_aot_app_sdl_tests>"
            VERBATIM)
        add_test(NAME clean_aot_app_sdl_tests COMMAND clean_aot_app_sdl_tests)
        set_tests_properties(clean_aot_app_sdl_tests PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    endif()
endif()

if(CLEAN_AOT_APP_BUILD_TESTS AND BUILD_TESTING)
    add_executable(clean_aot_app_tests tests/app_tests.cpp)
    target_link_libraries(clean_aot_app_tests PRIVATE clean_aot_app)
    clean_aot_app_warnings(clean_aot_app_tests)
    add_test(NAME clean_aot_app_tests COMMAND clean_aot_app_tests)
endif()
