function(agnc_apply_msvc_compat target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /utf-8)
        target_compile_definitions(${target_name} PRIVATE
            _CRT_SECURE_NO_WARNINGS
            _UNICODE
            UNICODE
        )
    endif()
endfunction()
