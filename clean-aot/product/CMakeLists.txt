cmake_minimum_required(VERSION 3.20)

# Consumer template for sources produced by tools/product/build_product.py.
# This target contains only generated native source.  It deliberately does not
# add the clean-AOT compiler, a compiler library, a JIT, an interpreter, or a
# legacy/emulator runtime to the link graph.
project(wipeout3se_clean_product_generated LANGUAGES C CXX)

set(CLEAN_AOT_PRODUCT_GENERATED_DIR "" CACHE PATH
    "External directory emitted by the clean-AOT product setup")
if(NOT CLEAN_AOT_PRODUCT_GENERATED_DIR)
    message(FATAL_ERROR
        "Set CLEAN_AOT_PRODUCT_GENERATED_DIR to an external generated output")
endif()

get_filename_component(_clean_aot_product_generated
    "${CLEAN_AOT_PRODUCT_GENERATED_DIR}" REALPATH)
set(_clean_aot_product_sources "${_clean_aot_product_generated}/sources.cmake")
if(NOT EXISTS "${_clean_aot_product_sources}")
    message(FATAL_ERROR "Missing generated source manifest: ${_clean_aot_product_sources}")
endif()
include("${_clean_aot_product_sources}")

if(NOT DEFINED CLEAN_AOT_PRODUCT_GENERATED_SOURCES OR
   NOT CLEAN_AOT_PRODUCT_GENERATED_SOURCES)
    message(FATAL_ERROR "Generated source manifest contains no C++ sources")
endif()

add_library(wipeout3se_product_generated STATIC
    ${CLEAN_AOT_PRODUCT_GENERATED_SOURCES}
)
target_compile_features(wipeout3se_product_generated PUBLIC cxx_std_20)
target_include_directories(wipeout3se_product_generated PUBLIC
    "${_clean_aot_product_generated}"
)

if(MSVC)
    target_compile_options(wipeout3se_product_generated PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(wipeout3se_product_generated PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
    )
endif()

# A clean runtime target is intentionally supplied by the consumer.  The
# template never names or discovers a legacy/emulator/compiler target.
if(DEFINED CLEAN_AOT_PRODUCT_RUNTIME_TARGET)
    if(NOT TARGET "${CLEAN_AOT_PRODUCT_RUNTIME_TARGET}")
        message(FATAL_ERROR
            "CLEAN_AOT_PRODUCT_RUNTIME_TARGET is not a configured clean target")
    endif()
    target_link_libraries(wipeout3se_product_generated
        PUBLIC "${CLEAN_AOT_PRODUCT_RUNTIME_TARGET}")
endif()
