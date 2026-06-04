# Hardware for easy file transfer and serial communication between Atari 16/32 bit computers and PC.

## Features:
* ACSI hard disk emulation using SD-card.
* Multiple hard disk images on same SD-card.
* Each hard disk image can selectively be exposed as USB drive to PC.
* Each hard disk image can selectively have read/write enabled.
* All hard disk formats (even TOS) can be read/written over USB.
* RS232 to USB.

## Target users:
* Developers using m68k-atari-dev towards non emulated Atari computers.
* Anyone looking for easy and fast file transfers between Atari and PC.

## Directory contents
kicad:  
KiCad project with schematics and PCB design.  

firmware:  
Raspberry Pi Pico sources for the hardware.  

## License:
This project is MIT licensed.  
The submodule: [no-OS-FatFS-SD-SPI-RPi-Pico](https://github.com/carlk3/no-OS-FatFS-SD-SPI-RPi-Pico) is a separate project and is Apache-2.0 licensed.  


