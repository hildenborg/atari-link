#ifndef PINS_DEFINED
#define PINS_DEFINED

// GPIO assignments for the two hardware variants.
// ATARI_LINK_SCSI is set by CMake when building the SCSI variant (RP2350B).
// The ACSI port pins are defined in acsi/acsi.cpp and acsi/acsi_asm.pio.

#ifdef ATARI_LINK_SCSI

// RS-232, UART0
#define PIN_UART_TX		44
#define PIN_UART_RX		45
#define PIN_UART_CTS	46
#define PIN_UART_RTS	47

// SD-Card, SPI0
#define PIN_SD_MISO		36
#define PIN_SD_CS		37
#define PIN_SD_SCK		38
#define PIN_SD_MOSI		39

// SCSI bus. All pins used by the PIO must be below 32.
// Data and DBP go through a 74LVT245 (DB0-7) and a 74LVT125 (DBP).
#define PIN_SCSI_DB0		2	// DB0-DB7 are 2-9
#define PIN_SCSI_DBP		10	// 74LVT125 /OE, low asserts DBP
// 74LVT125 /OE, low asserts the bus signal
#define PIN_SCSI_REQ		11
#define PIN_SCSI_BSY_OUT	12
#define PIN_SCSI_MSG		13
#define PIN_SCSI_CD			14
#define PIN_SCSI_IO			15
// Data bus 74LVT245 control
#define PIN_SCSI_DATA_DIR	16	// High: Pico drives the bus. Low: Pico reads the bus.
#define PIN_SCSI_DATA_OE	17	// Low enables the 74LVT245
// Bus inputs through a 74LVT245, low when asserted
#define PIN_SCSI_ACK		18
#define PIN_SCSI_ATN		19
#define PIN_SCSI_SEL		20
#define PIN_SCSI_RST		21
#define PIN_SCSI_BSY_IN		22
#define PIN_SCSI_DBP_IN		24

#else // ACSI variant (RP2040 Pico)

// RS-232, UART0
#define PIN_UART_TX		0
#define PIN_UART_RX		1
#define PIN_UART_CTS	2
#define PIN_UART_RTS	3

// SD-Card, SPI0
#define PIN_SD_MISO		16
#define PIN_SD_CS		17
#define PIN_SD_SCK		18
#define PIN_SD_MOSI		19

#endif

#endif // PINS_DEFINED
