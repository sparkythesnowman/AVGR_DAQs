# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

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

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(peripheral_logic_analyzer C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(peripheral_logic_analyzer
    logic_analyser.c
    spi_central_config.c
)

pico_generate_pio_header(peripheral_logic_analyzer ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)

target_link_libraries(peripheral_logic_analyzer PRIVATE pico_stdlib hardware_pio hardware_dma hardware_clocks hardware_spi)
pico_add_extra_outputs(peripheral_logic_analyzer)

# Enable USB stdio, disable UART stdio for this target
pico_enable_stdio_usb(peripheral_logic_analyzer 1)
pico_enable_stdio_uart(peripheral_logic_analyzer 0)

# add url via pico_set_program_url