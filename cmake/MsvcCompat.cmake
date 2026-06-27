function(agnc_apply_msvc_compat target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /utf-8)
        # /wd6297 /wd28182: false-positive analyzer di header yyjson (vcpkg), bukan kode agnc.
        target_compile_options(${target_name} PRIVATE /wd6297 /wd28182)
        target_compile_definitions(${target_name} PRIVATE
            _CRT_SECURE_NO_WARNINGS
            _UNICODE
            UNICODE
        )
    endif()
endfunction()
