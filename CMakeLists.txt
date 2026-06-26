cmake_minimum_required(VERSION 3.20)

project(agnc VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Visual Studio Open Folder memakai preset x64-Debug dengan output di out/build/.
if(CMAKE_GENERATOR MATCHES "Ninja" AND MSVC)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

include(cmake/CompilerWarnings.cmake)

add_executable(agnc
    src/main.c
    src/cli/args.c
    src/cli/doctor.c
    src/util/path.c
    src/util/status.c
)

target_include_directories(agnc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

agnc_set_compiler_warnings(agnc)

if(WIN32)
    target_compile_definitions(agnc PRIVATE AGNC_PLATFORM_WINDOWS=1)
elseif(APPLE)
    target_compile_definitions(agnc PRIVATE AGNC_PLATFORM_MACOS=1)
else()
    target_compile_definitions(agnc PRIVATE AGNC_PLATFORM_LINUX=1)
endif()

install(TARGETS agnc RUNTIME DESTINATION bin)
