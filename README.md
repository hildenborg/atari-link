## About:
The purpose of Atari-Link is to give developers using [m68k-atari-dev toolchain](https://github.com/hildenborg/m68k-atari-dev) the possibillity of developing for real Atari computers the same way they are used to target the Hatari emulator.  
If you aren't a developer and only want a ACSI SD-Card hard disk emulator, then there are many other options with better support.  
The [wiki](https://github.com/hildenborg/atari-link/wiki) is recommended for deeper information  

## Hardware:
The hardware is designed in KiCad and the project can be found in the "kicad" directory.  
It is designed to be "easy" to build by hand, and the wiki have more information about that.

## Firmware:
The hardware is based around the Raspberry Pico and the formware source is in the "firmware" directory.  
The project is dependent on a submodule, so do not forget to do `git submodule init` and `git submodule update` after cloning this git repository.  

## Setup:
If you have built the hardware, and just want to get going, then download [atari-link.zip](https://github.com/hildenborg/atari-link/releases/download/Release1/atari-link.zip) and extract its contents.  
Flash the Pico with "atari_link.uf2" and put "config.txt, "tos32mb_ahdi5_boot.img" and "tos32mb_gdbsrv.img" on a FAT32 formatted SD-card.  
This will set you up with a development system that should work on pretty much anything Atari have made.  
You may need to edit the "config.txt" file if you have other hard drives connected, so there aren't any device id collision.  
The hard disk images in the release is TOS 32MB formatted images so they should work on just about anything.  
The "config.txt" in the release is set up so the "tos32mb_ahdi5_boot.img" disk image is not seen by the PC and the "tos32mb_gdbsrv.img" is read-only by the Atari. The "tos32mb_gdbsrv.img" is visible on the PC as a USB drive and files written to it are immediately accessible on the Atari.  
NOTE: Even TOS formatted hard disk images are accessible to the PC as a USB drive. There's a [wiki page](https://github.com/hildenborg/atari-link/wiki/ProblemsSolutions) that describes how it works.

## License:
This project is MIT licensed.  
The submodule: [no-OS-FatFS-SD-SPI-RPi-Pico](https://github.com/carlk3/no-OS-FatFS-SD-SPI-RPi-Pico) is a separate project and is Apache-2.0 licensed.  


