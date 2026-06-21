#!/bin/bash

#	Copyright (C) 2025 Mikael Hildenborg
#	SPDX-License-Identifier: MIT

# This is the extension less name of your executable.
export TARGET_NAME=hello

# Set this to where your Atari-Link USB drive is.
export ST_HD_PATH=/media/$USER/B00B-5000

# Set this to where your toolchain is.
export TOOLCHAIN_M68K=$HOME/toolchain/m68k-atari-elf

# This is the Atari-Link serial port
export GDB_COM_PORT=/dev/ttyACM0

# Start VsCode
code .

