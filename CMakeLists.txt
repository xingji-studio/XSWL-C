cmake_minimum_required(VERSION 3.20)

project(XSWL-C LANGUAGES C)

option(XSWL_ENABLE_DEV_SANITIZERS "Enable ASan/UBSan for Debug builds" OFF)
option(XSWL_ENABLE_TRACE "Enable DEBUG_TRACE logging" OFF)
option(XSWL_ENABLE_NATIVE_RELEASE "Use -march=native for Release builds" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(UNICORN REQUIRED IMPORTED_TARGET unicorn)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)

set(XSWL_COMMON_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    -Wcast-align
    -Wstrict-prototypes
    -Wmissing-prototypes
    -Wold-style-definition
    -Wno-pointer-to-int-cast
)

function(xswl_apply_common_settings target_name)
    target_include_directories(${target_name}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_compile_options(${target_name}
        PRIVATE
            ${XSWL_COMMON_WARNINGS}
            $<$<CONFIG:Debug>:-O0>
            $<$<CONFIG:Debug>:-g>
            $<$<CONFIG:Release>:-O3>
            $<$<CONFIG:Release>:-DNDEBUG>
    )

    if(XSWL_ENABLE_TRACE)
        target_compile_definitions(${target_name} PRIVATE DEBUG_TRACE)
    endif()

    if(XSWL_ENABLE_NATIVE_RELEASE)
        target_compile_options(${target_name} PRIVATE $<$<CONFIG:Release>:-march=native>)
    endif()

    if(XSWL_ENABLE_DEV_SANITIZERS)
        target_compile_options(${target_name}
            PRIVATE
                $<$<CONFIG:Debug>:-fsanitize=address,undefined>
                $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
        )
        target_link_options(${target_name}
            PRIVATE
                $<$<CONFIG:Debug>:-fsanitize=address,undefined>
        )
    endif()
endfunction()

add_executable(xswl
    src/main.c
    src/xj380_emu.c
    src/xj380_gui.c
    src/xj380_native.c
)

xswl_apply_common_settings(xswl)

target_compile_definitions(xswl
    PRIVATE
        XJ380_GUI
)

target_compile_options(xswl
    PRIVATE
        -Wno-pedantic
)

target_link_libraries(xswl
    PRIVATE
        PkgConfig::UNICORN
        PkgConfig::SDL3
        m
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/test_gui_events.elf
    COMMAND ${CMAKE_C_COMPILER}
        -m64 -nostdlib -static -no-pie
        -Wl,-Ttext=0x200000
        -o ${CMAKE_CURRENT_BINARY_DIR}/test_gui_events.elf
        ${CMAKE_CURRENT_SOURCE_DIR}/test_gui_events.S
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_gui_events.S
    VERBATIM
)

add_custom_target(xswl_test_gui_events_file
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/test_gui_events.elf
)

add_custom_target(xswl_run_gui_events
    COMMAND ${CMAKE_COMMAND} -E env
        SDL_VIDEODRIVER=dummy
        LSAN_OPTIONS=detect_leaks=0
        XSWL_TEST_GUI_EVENTS=1
        $<TARGET_FILE:xswl>
        ${CMAKE_CURRENT_BINARY_DIR}/test_gui_events.elf
    DEPENDS xswl xswl_test_gui_events_file
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/test_native_smoke.elf
    COMMAND ${CMAKE_C_COMPILER}
        -m64 -nostdlib -static -no-pie
        -Wl,-Ttext=0x200000
        -o ${CMAKE_CURRENT_BINARY_DIR}/test_native_smoke.elf
        ${CMAKE_CURRENT_SOURCE_DIR}/test_native_smoke.S
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_native_smoke.S
    VERBATIM
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/native_fixture.txt
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/test_native_fixture.txt
        ${CMAKE_CURRENT_BINARY_DIR}/native_fixture.txt
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_native_fixture.txt
    VERBATIM
)

add_custom_target(xswl_test_native_smoke_file
    DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/test_native_smoke.elf
        ${CMAKE_CURRENT_BINARY_DIR}/native_fixture.txt
)

add_custom_target(xswl_run_native_smoke
    COMMAND $<TARGET_FILE:xswl>
        --native
        nodebug
        ${CMAKE_CURRENT_BINARY_DIR}/test_native_smoke.elf
    DEPENDS xswl xswl_test_native_smoke_file
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
)
