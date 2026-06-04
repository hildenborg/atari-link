cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)
project(acsi_project C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

#add_compile_definitions(SERIAL_LOGGING)

add_executable(acsi
  main.cpp
  acsi.cpp
  image.cpp
  config.cpp
  spi_config.cpp
)

#add_subdirectory (FatFs)
add_subdirectory(no-OS-FatFS-SD-SPI-RPi-Pico/FatFs_SPI build)
add_subdirectory (Usb)
target_link_libraries(acsi LINK_PUBLIC
	pico_stdlib
	pico_multicore
	hardware_pio
	hardware_dma
	USB
	FatFs_SPI
#	FatFs
	)
	
pico_enable_stdio_usb(acsi 0)
pico_enable_stdio_uart(acsi 0)

pico_generate_pio_header(acsi ${CMAKE_CURRENT_LIST_DIR}/acsi_asm.pio)

pico_add_extra_outputs(acsi)

	