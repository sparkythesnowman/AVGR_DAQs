cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(double_DAQ C CXX ASM)

pico_sdk_init()

# ── FatFS/SD library (central only) ──────────────────────────────────────────
add_subdirectory(
    central/no-OS-FatFS-SD-SPI-RPi-Pico-master/FatFs_SPI
    build_fatfs_spi
)

# ── Peripheral target ─────────────────────────────────────────────────────────
add_executable(peripheral_logic_analyzer
    peripheral/logic_analyser.c
    peripheral/spi_slave_config.c
    peripheral/neopixel.c
    peripheral/capture.c
)

pico_generate_pio_header(peripheral_logic_analyzer ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)

target_include_directories(peripheral_logic_analyzer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/peripheral)

target_link_libraries(peripheral_logic_analyzer PRIVATE
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_clocks
    hardware_spi
)

pico_enable_stdio_usb(peripheral_logic_analyzer 1)
pico_enable_stdio_uart(peripheral_logic_analyzer 0)
pico_add_extra_outputs(peripheral_logic_analyzer)

# ── Central target ────────────────────────────────────────────────────────────
add_executable(central_logic_analyzer
    central/logic_analyser.c
    central/hw_config.c
    central/flash_log.c
    central/neopixel.c
    central/sd_config.c
)

pico_generate_pio_header(central_logic_analyzer ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)

target_include_directories(central_logic_analyzer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/central)

target_link_libraries(central_logic_analyzer PRIVATE
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_flash
    hardware_sync
    hardware_clocks
    hardware_spi
    FatFs_SPI
)

pico_enable_stdio_usb(central_logic_analyzer 1)
pico_enable_stdio_uart(central_logic_analyzer 0)
pico_add_extra_outputs(central_logic_analyzer)