if(NOT DEFINED DXC_EXECUTABLE OR NOT DEFINED SHADER_SOURCE_DIR OR
   NOT DEFINED OUTPUT_HEADER)
    message(FATAL_ERROR "DXC_EXECUTABLE, SHADER_SOURCE_DIR, and OUTPUT_HEADER are required")
endif()

execute_process(
    COMMAND "${DXC_EXECUTABLE}" --version
    RESULT_VARIABLE DXC_VERSION_RESULT
    OUTPUT_VARIABLE DXC_VERSION_OUTPUT
    ERROR_VARIABLE DXC_VERSION_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
)
if(NOT DXC_VERSION_RESULT EQUAL 0 OR
   NOT DXC_VERSION_OUTPUT MATCHES "1\\.9\\.2602\\.17")
    message(FATAL_ERROR
        "The host shader path requires official DXC 1.9.2602.17; got '${DXC_VERSION_OUTPUT}${DXC_VERSION_ERROR}'")
endif()

get_filename_component(OUTPUT_DIRECTORY "${OUTPUT_HEADER}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
set(VERTEX_OUTPUT "${OUTPUT_DIRECTORY}/present.vert.spv")
set(FRAGMENT_OUTPUT "${OUTPUT_DIRECTORY}/present.frag.spv")

execute_process(
    COMMAND "${DXC_EXECUTABLE}"
        "${SHADER_SOURCE_DIR}/present.vert.hlsl"
        -E VSMain -T vs_6_0 -spirv -fspv-target-env=vulkan1.2
        -fvk-use-dx-position-w -Fo "${VERTEX_OUTPUT}"
    RESULT_VARIABLE VERTEX_RESULT
    OUTPUT_VARIABLE VERTEX_STDOUT
    ERROR_VARIABLE VERTEX_STDERR
)
if(NOT VERTEX_RESULT EQUAL 0)
    message(FATAL_ERROR "Vertex shader compilation failed: ${VERTEX_STDOUT}${VERTEX_STDERR}")
endif()

execute_process(
    COMMAND "${DXC_EXECUTABLE}"
        "${SHADER_SOURCE_DIR}/present.frag.hlsl"
        -E PSMain -T ps_6_0 -spirv -fspv-target-env=vulkan1.2
        -Fo "${FRAGMENT_OUTPUT}"
    RESULT_VARIABLE FRAGMENT_RESULT
    OUTPUT_VARIABLE FRAGMENT_STDOUT
    ERROR_VARIABLE FRAGMENT_STDERR
)
if(NOT FRAGMENT_RESULT EQUAL 0)
    message(FATAL_ERROR "Fragment shader compilation failed: ${FRAGMENT_STDOUT}${FRAGMENT_STDERR}")
endif()

function(read_spirv_words INPUT_FILE OUTPUT_VARIABLE)
    file(SIZE "${INPUT_FILE}" INPUT_SIZE)
    math(EXPR REMAINDER "${INPUT_SIZE} % 4")
    if(NOT REMAINDER EQUAL 0 OR INPUT_SIZE LESS 20)
        message(FATAL_ERROR "${INPUT_FILE} is not a complete SPIR-V module")
    endif()
    file(READ "${INPUT_FILE}" HEX_DATA HEX)
    string(SUBSTRING "${HEX_DATA}" 0 8 MAGIC)
    if(NOT MAGIC STREQUAL "03022307")
        message(FATAL_ERROR "${INPUT_FILE} has an invalid SPIR-V magic word")
    endif()
    string(LENGTH "${HEX_DATA}" HEX_LENGTH)
    math(EXPR WORD_COUNT "${HEX_LENGTH} / 8")
    set(WORDS "")
    math(EXPR LAST_WORD "${WORD_COUNT} - 1")
    foreach(INDEX RANGE 0 ${LAST_WORD})
        math(EXPR OFFSET "${INDEX} * 8")
        string(SUBSTRING "${HEX_DATA}" ${OFFSET} 2 B0)
        math(EXPR OFFSET1 "${OFFSET} + 2")
        string(SUBSTRING "${HEX_DATA}" ${OFFSET1} 2 B1)
        math(EXPR OFFSET2 "${OFFSET} + 4")
        string(SUBSTRING "${HEX_DATA}" ${OFFSET2} 2 B2)
        math(EXPR OFFSET3 "${OFFSET} + 6")
        string(SUBSTRING "${HEX_DATA}" ${OFFSET3} 2 B3)
        string(APPEND WORDS "0x${B3}${B2}${B1}${B0}U, ")
    endforeach()
    set(${OUTPUT_VARIABLE} "${WORDS}" PARENT_SCOPE)
endfunction()

file(SHA256 "${VERTEX_OUTPUT}" VERTEX_SHA256)
file(SHA256 "${FRAGMENT_OUTPUT}" FRAGMENT_SHA256)
read_spirv_words("${VERTEX_OUTPUT}" VERTEX_WORDS)
read_spirv_words("${FRAGMENT_OUTPUT}" FRAGMENT_WORDS)

file(WRITE "${OUTPUT_HEADER}" "#pragma once\n\n")
file(APPEND "${OUTPUT_HEADER}" "#include <cstddef>\n#include <cstdint>\n\n")
file(APPEND "${OUTPUT_HEADER}" "namespace clean_aot::host::shaders {\n\n")
file(APPEND "${OUTPUT_HEADER}" "inline constexpr std::uint32_t kPresentVertexSpirv[] = {${VERTEX_WORDS}};\n")
file(APPEND "${OUTPUT_HEADER}" "inline constexpr std::size_t kPresentVertexSpirvWordCount = sizeof(kPresentVertexSpirv) / sizeof(kPresentVertexSpirv[0]);\n")
file(APPEND "${OUTPUT_HEADER}" "inline constexpr char kPresentVertexSpirvSha256[] = \"${VERTEX_SHA256}\";\n\n")
file(APPEND "${OUTPUT_HEADER}" "inline constexpr std::uint32_t kPresentFragmentSpirv[] = {${FRAGMENT_WORDS}};\n")
file(APPEND "${OUTPUT_HEADER}" "inline constexpr std::size_t kPresentFragmentSpirvWordCount = sizeof(kPresentFragmentSpirv) / sizeof(kPresentFragmentSpirv[0]);\n")
file(APPEND "${OUTPUT_HEADER}" "inline constexpr char kPresentFragmentSpirvSha256[] = \"${FRAGMENT_SHA256}\";\n\n")
file(APPEND "${OUTPUT_HEADER}" "static_assert(kPresentVertexSpirv[0] == 0x07230203U);\n")
file(APPEND "${OUTPUT_HEADER}" "static_assert(kPresentFragmentSpirv[0] == 0x07230203U);\n\n")
file(APPEND "${OUTPUT_HEADER}" "} // namespace clean_aot::host::shaders\n")
