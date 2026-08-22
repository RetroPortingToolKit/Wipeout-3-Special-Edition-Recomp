function(clean_aot_colocate_sdl3 target)
    if(NOT TARGET "${target}" OR NOT TARGET SDL3::SDL3)
        message(FATAL_ERROR "SDL3 colocation requires existing target ${target} and SDL3::SDL3")
    endif()
    add_custom_command(TARGET "${target}" POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:${target}>"
        VERBATIM)
    set_property(TARGET "${target}" PROPERTY CLEAN_AOT_SDL3_COLOCATED TRUE)
endfunction()
