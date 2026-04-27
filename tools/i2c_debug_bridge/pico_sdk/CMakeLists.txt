cmake_minimum_required(VERSION 3.13)

if(NOT PICO_SDK_PATH)
    if(DEFINED ENV{PICO_SDK_PATH})
        set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
    elseif(EXISTS "$ENV{HOME}/.pico-sdk/sdk/2.2.0")
        set(PICO_SDK_PATH "$ENV{HOME}/.pico-sdk/sdk/2.2.0")
    endif()
endif()

include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")

project(itaiko_i2c_debug_bridge C CXX ASM)
pico_sdk_init()

add_executable(itaiko_i2c_debug_bridge
    main.c
)

target_link_libraries(itaiko_i2c_debug_bridge
    pico_stdlib
    hardware_i2c
    pico_i2c_slave
)

pico_enable_stdio_usb(itaiko_i2c_debug_bridge 1)
pico_enable_stdio_uart(itaiko_i2c_debug_bridge 0)
pico_add_extra_outputs(itaiko_i2c_debug_bridge)
