# cmake/Vcpkg.cmake
#
# Temukan toolchain vcpkg sebelum project() agar find_package(yyjson) berhasil
# saat configure dari Visual Studio Open Folder (tanpa menjalankan build.ps1).

if(DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE)
    return()
endif()

set(_AGNC_VCPKG_ROOT "")

if(DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
    set(_AGNC_VCPKG_ROOT "$ENV{VCPKG_ROOT}")
endif()

if(_AGNC_VCPKG_ROOT STREQUAL "")
    set(_AGNC_VS_SEARCH_ROOTS
        "$ENV{ProgramFiles}/Microsoft Visual Studio"
        "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio"
        "D:/Program Files/Microsoft Visual Studio"
    )
    foreach(_vs_root IN LISTS _AGNC_VS_SEARCH_ROOTS)
        if(NOT EXISTS "${_vs_root}")
            continue()
        endif()
        file(GLOB _vs_editions LIST_DIRECTORIES true "${_vs_root}/*")
        foreach(_edition IN LISTS _vs_editions)
            set(_candidate "${_edition}/VC/vcpkg")
            if(EXISTS "${_candidate}/scripts/buildsystems/vcpkg.cmake")
                set(_AGNC_VCPKG_ROOT "${_candidate}")
                break()
            endif()
        endforeach()
        if(NOT _AGNC_VCPKG_ROOT STREQUAL "")
            break()
        endif()
    endforeach()
endif()

if(NOT _AGNC_VCPKG_ROOT STREQUAL "")
    set(CMAKE_TOOLCHAIN_FILE
        "${_AGNC_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "vcpkg toolchain" FORCE)
    set(VCPKG_MANIFEST_MODE ON CACHE BOOL "vcpkg manifest mode" FORCE)
    message(STATUS "agnc: using vcpkg toolchain at ${_AGNC_VCPKG_ROOT}")
endif()

unset(_AGNC_VCPKG_ROOT)
unset(_AGNC_VS_SEARCH_ROOTS)
unset(_vs_root)
unset(_vs_editions)
unset(_edition)
unset(_candidate)
