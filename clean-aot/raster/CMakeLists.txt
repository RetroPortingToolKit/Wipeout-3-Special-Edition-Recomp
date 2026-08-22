cmake_minimum_required(VERSION 3.20)

project(clean_aot_raster LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CLEAN_AOT_RASTER_BUILD_TESTS "Build the standalone raster tests" ON)
option(CLEAN_AOT_RASTER_ENABLE_SANITIZERS
       "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(CLEAN_AOT_GPU_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../gpu/include")
if(NOT EXISTS "${CLEAN_AOT_GPU_INCLUDE_DIR}/clean_aot/gpu/gpu.hpp")
  message(FATAL_ERROR "clean-aot/gpu public headers are required")
endif()

add_library(clean_aot_raster STATIC
  src/raster.cpp
)

target_compile_features(clean_aot_raster PUBLIC cxx_std_20)
target_include_directories(clean_aot_raster
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CLEAN_AOT_GPU_INCLUDE_DIR}
)

if(MSVC)
  target_compile_options(clean_aot_raster PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(clean_aot_raster PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror
  )
endif()

if(CLEAN_AOT_RASTER_ENABLE_SANITIZERS AND NOT MSVC)
  target_compile_options(clean_aot_raster PRIVATE
    -fsanitize=address,undefined -fno-omit-frame-pointer
    -fno-sanitize-recover=all
  )
  target_link_options(clean_aot_raster PRIVATE
    -fsanitize=address,undefined -fno-sanitize-recover=all
  )
endif()

include(CTest)

if(CLEAN_AOT_RASTER_BUILD_TESTS)
  add_executable(clean_aot_raster_tests
    tests/raster_tests.cpp
  )
  target_link_libraries(clean_aot_raster_tests PRIVATE clean_aot_raster)
  target_compile_features(clean_aot_raster_tests PRIVATE cxx_std_20)
  if(MSVC)
    target_compile_options(clean_aot_raster_tests PRIVATE /W4 /WX /permissive-)
  else()
    target_compile_options(clean_aot_raster_tests PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror
    )
  endif()
  if(CLEAN_AOT_RASTER_ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(clean_aot_raster_tests PRIVATE
      -fsanitize=address,undefined -fno-omit-frame-pointer
      -fno-sanitize-recover=all
    )
    target_link_options(clean_aot_raster_tests PRIVATE
      -fsanitize=address,undefined -fno-omit-frame-pointer
      -fno-sanitize-recover=all
    )
  endif()
  add_test(NAME clean_aot_raster_tests COMMAND clean_aot_raster_tests)
endif()
