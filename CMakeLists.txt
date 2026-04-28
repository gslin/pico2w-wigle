cmake_minimum_required(VERSION 3.13)

# Default board is Pico 2 W (RP2350 + CYW43439 Wi-Fi). Override with
# `cmake -DPICO_BOARD=...` if needed.
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico2_w)
endif()

include(pico_sdk_import.cmake)

project(pico2w_wigle C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

if(NOT PICO_CYW43_SUPPORTED)
    message(FATAL_ERROR
        "PICO_BOARD=${PICO_BOARD} does not enable CYW43 Wi-Fi support. "
        "Set -DPICO_BOARD=pico2_w (or another CYW43-equipped board).")
endif()

add_executable(pico2w-wigle
    src/main.c
    src/wifi_scan.c
    src/gps.c
    src/wigle_upload.c
    src/csv_buffer.c
)

target_include_directories(pico2w-wigle PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_compile_definitions(pico2w-wigle PRIVATE
    # The escaped quotes are necessary: cmake passes the raw value as
    # the body of -D, so we need the inner quotes to survive the shell.
    CYW43_HOST_NAME=\"pico2w-wigle\"
    MBEDTLS_CONFIG_FILE=\"mbedtls_config.h\"
)

target_compile_options(pico2w-wigle PRIVATE
    -Wall -Wextra -Wno-unused-parameter
)

target_link_libraries(pico2w-wigle PRIVATE
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_mbedtls
    pico_mbedtls
    hardware_uart
)

# stdio over USB CDC keeps a serial console on the same cable that
# powers the board, which is convenient when wardriving on foot.
pico_enable_stdio_usb(pico2w-wigle 1)
pico_enable_stdio_uart(pico2w-wigle 0)

pico_add_extra_outputs(pico2w-wigle)
