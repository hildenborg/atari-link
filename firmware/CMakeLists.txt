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

# Hardware variant: ACSI (Raspberry Pico, RP2040) or SCSI (Atari-Link SCSI board, RP2350B).
# Use a separate build directory for each variant, e.g.: cmake -B build_scsi -DATARI_LINK_VARIANT=SCSI
set(ATARI_LINK_VARIANT ACSI CACHE STRING "Hardware variant: ACSI or SCSI")
set_property(CACHE ATARI_LINK_VARIANT PROPERTY STRINGS ACSI SCSI)
if (ATARI_LINK_VARIANT STREQUAL "SCSI")
	# Board definition in src/board/atari_link_scsi.h
	list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/src/board)
	set(PICO_BOARD atari_link_scsi CACHE STRING "Board type")
else()
	set(PICO_BOARD pico CACHE STRING "Board type")
endif()

include(pico_sdk_import.cmake)

project(atari_link C CXX ASM)

#add_compile_definitions(SERIAL_LOGGING)
pico_sdk_init()

add_executable(atari_link
  src/main.cpp
  src/config/config.cpp
  src/drive/acsi_interface.cpp
  src/drive/drive_collection.cpp
  src/drive/drive_interface.cpp
  src/drive/fatfs_lock.cpp
  src/drive/lun.cpp
  src/drive/usb_diff_log.cpp
  src/drive/usb_interface.cpp
  src/sd/spi_config.cpp
  src/usb/msc_disk.cpp
  src/usb/serial.cpp
  src/usb/usb.cpp
  src/usb/usb_descriptors.c
)

if (ATARI_LINK_VARIANT STREQUAL "SCSI")
	target_sources(atari_link PRIVATE src/scsi/scsi.cpp)
	target_compile_definitions(atari_link PRIVATE ATARI_LINK_SCSI)
	pico_generate_pio_header(atari_link ${CMAKE_CURRENT_LIST_DIR}/src/scsi/scsi_bus.pio)
	set_target_properties(atari_link PROPERTIES OUTPUT_NAME atari_link_scsi)
else()
	target_sources(atari_link PRIVATE src/acsi/acsi.cpp)
	pico_generate_pio_header(atari_link ${CMAKE_CURRENT_LIST_DIR}/src/acsi/acsi_asm.pio)
endif()

# Includes are written relative to the source folder, e.g. "drive/image.h".
target_include_directories(atari_link PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)

target_compile_definitions(atari_link PRIVATE
  CFG_TUSB_CONFIG_FILE="${CMAKE_CURRENT_LIST_DIR}/src/usb/tusb_config.h"
)

pico_set_program_name(atari_link "atari-link")
pico_set_program_version(atari_link "0.1")

add_subdirectory(no-OS-FatFS-SD-SPI-RPi-Pico/FatFs_SPI build)
if (ATARI_LINK_VARIANT STREQUAL "SCSI")
	# The RP2350 has no RTC. Remove the library's RTC code; src/sd/fattime.c replaces it.
	get_target_property(FATFS_SOURCES FatFs_SPI INTERFACE_SOURCES)
	list(FILTER FATFS_SOURCES EXCLUDE REGEX "/src/rtc\\.c$")
	set_target_properties(FatFs_SPI PROPERTIES INTERFACE_SOURCES "${FATFS_SOURCES}")
	get_target_property(FATFS_LIBS FatFs_SPI INTERFACE_LINK_LIBRARIES)
	list(REMOVE_ITEM FATFS_LIBS hardware_rtc)
	set_target_properties(FatFs_SPI PROPERTIES INTERFACE_LINK_LIBRARIES "${FATFS_LIBS}")
	target_sources(atari_link PRIVATE src/sd/fattime.c)
endif()
target_link_libraries(atari_link LINK_PUBLIC
	pico_stdlib
	pico_multicore
	hardware_pio
	hardware_dma
	tinyusb_device
	tinyusb_board
	FatFs_SPI
	)
	
pico_enable_stdio_usb(atari_link 0)
pico_enable_stdio_uart(atari_link 0)

pico_add_extra_outputs(atari_link)

	