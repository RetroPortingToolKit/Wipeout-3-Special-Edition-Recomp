cmake_minimum_required(VERSION 3.20)

project(clean_aot_sio LANGUAGES CXX)

include(CTest)

add_library(clean_aot_sio STATIC
    sio.cpp
)

target_compile_features(clean_aot_sio PUBLIC cxx_std_20)
target_include_directories(clean_aot_sio
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
    target_compile_options(clean_aot_sio PRIVATE /W4 /WX /permissive-)
else()
    set(CLEAN_AOT_SIO_STRICT_WARNINGS
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Werror
    )
    target_compile_options(clean_aot_sio PRIVATE ${CLEAN_AOT_SIO_STRICT_WARNINGS})
endif()

add_executable(clean_aot_sio_tests
    tests/sio_tests.cpp
)
target_compile_features(clean_aot_sio_tests PRIVATE cxx_std_20)
target_link_libraries(clean_aot_sio_tests PRIVATE clean_aot_sio)

if(MSVC)
    target_compile_options(clean_aot_sio_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(clean_aot_sio_tests PRIVATE ${CLEAN_AOT_SIO_STRICT_WARNINGS})
endif()

if(CLEAN_AOT_SIO_SANITIZERS AND NOT MSVC)
    target_compile_options(clean_aot_sio PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all)
    target_link_options(clean_aot_sio PRIVATE -fsanitize=address,undefined)
    target_compile_options(clean_aot_sio_tests PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all)
    target_link_options(clean_aot_sio_tests PRIVATE -fsanitize=address,undefined)
endif()

if(BUILD_TESTING)
    add_test(NAME clean_aot_sio_tests COMMAND clean_aot_sio_tests)
endif()
