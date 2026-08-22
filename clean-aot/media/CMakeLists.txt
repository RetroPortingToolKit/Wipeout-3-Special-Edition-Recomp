cmake_minimum_required(VERSION 3.20)

project(clean_aot_media LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CLEAN_AOT_MEDIA_ENABLE_SANITIZERS
       "Enable AddressSanitizer and UndefinedBehaviorSanitizer"
       OFF)

add_library(clean_aot_media STATIC
    src/media.cpp
)

target_compile_features(clean_aot_media PUBLIC cxx_std_20)
target_include_directories(clean_aot_media
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(clean_aot_media PRIVATE /W4 /WX /permissive-)
else()
    set(CLEAN_AOT_MEDIA_STRICT_WARNINGS
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Werror
    )
    target_compile_options(clean_aot_media PRIVATE ${CLEAN_AOT_MEDIA_STRICT_WARNINGS})
endif()

add_executable(clean_aot_media_tests
    tests/media_tests.cpp
)
target_compile_features(clean_aot_media_tests PRIVATE cxx_std_20)
target_link_libraries(clean_aot_media_tests PRIVATE clean_aot_media)

if(MSVC)
    target_compile_options(clean_aot_media_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(clean_aot_media_tests PRIVATE ${CLEAN_AOT_MEDIA_STRICT_WARNINGS})
endif()

if(CLEAN_AOT_MEDIA_ENABLE_SANITIZERS)
    if(MSVC)
        message(FATAL_ERROR "The media sanitizer configuration requires GCC or Clang")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        set(CLEAN_AOT_MEDIA_SANITIZERS
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_compile_options(clean_aot_media PRIVATE ${CLEAN_AOT_MEDIA_SANITIZERS})
        target_compile_options(clean_aot_media_tests PRIVATE ${CLEAN_AOT_MEDIA_SANITIZERS})
        target_link_options(clean_aot_media_tests PRIVATE -fsanitize=address,undefined)
    else()
        message(FATAL_ERROR "Unsupported compiler for the media sanitizer configuration")
    endif()
endif()

if(BUILD_TESTING)
    add_test(NAME clean_aot_media_tests COMMAND clean_aot_media_tests)
endif()
