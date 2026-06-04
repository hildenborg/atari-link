
add_library(USB INTERFACE)

#target_include_directories(USB SYSTEM BEFORE INTERFACE ${CMAKE_CURRENT_LIST_DIR})

target_compile_definitions(USB INTERFACE CFG_TUSB_CONFIG_FILE="${CMAKE_CURRENT_LIST_DIR}/tusb_config.h")

target_sources(USB INTERFACE
	usb.cpp
	msc_disk.cpp
	usb_descriptors.c
)

target_link_libraries(USB INTERFACE
	pico_stdlib
	tinyusb_device
	tinyusb_board
	)
