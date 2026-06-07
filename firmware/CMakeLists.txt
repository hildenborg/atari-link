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
cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

set(PICO_BOARD pico CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(atari_link C CXX ASM)

#add_compile_definitions(SERIAL_LOGGING)
pico_sdk_init()

add_executable(atari_link
  main.cpp
  acsi.cpp
  image.cpp
  config.cpp
  spi_config.cpp
)

pico_set_program_name(atari_link "atari-link")
pico_set_program_version(atari_link "0.1")

#add_subdirectory (FatFs)
add_subdirectory(no-OS-FatFS-SD-SPI-RPi-Pico/FatFs_SPI build)
add_subdirectory (Usb)
target_link_libraries(atari_link LINK_PUBLIC
	pico_stdlib
	pico_multicore
	hardware_pio
	hardware_dma
	USB
	FatFs_SPI
	)
	
pico_enable_stdio_usb(atari_link 0)
pico_enable_stdio_uart(atari_link 0)

pico_generate_pio_header(atari_link ${CMAKE_CURRENT_LIST_DIR}/acsi_asm.pio)

pico_add_extra_outputs(atari_link)

	