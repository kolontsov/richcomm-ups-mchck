Simple USB UPS interface using MC HCK
===

### DESCRIPTION

I needed to build USB device that is recognizable as UPS by [Network UPS Tools](http://www.networkupstools.org) (NUT).

It seems that one of the simplest USB drivers in NUT is [drivers/richcomm_usb.c](https://github.com/networkupstools/nut/blob/master/drivers/richcomm_usb.c). 

This code implements richcomm\_usb.c protocol using [MC HCK](https://mchck.org/about) prototype board based on Freescale [Kinetis K20 MCU](https://github.com/mchck/mchck/wiki/MCU-Features---MK20DX32VLF5) (ARM Cortex-M4). I just had spare kit and wanted to try the SDK, so the code has a lot of comments.

### REQUIREMENTS

* MC HCK board with standard bootloader
* [GCC ARM embedded toolchain](https://launchpad.net/gcc-arm-embedded/+download)
* [MC HCK toolchain](http://github.com/mchck/mchck)
* [dfu-util](https://gitorious.org/dfu-util)


### BUILD AND INSTALL

* Clone this repository to *examples/* subdirectory of MC HCK toolchain.
* `cd richcomm-ups-mchck; make`
* Plug MC HCK into USB port; type `make flash`, press push button on the board, wait ~1 sec and press __Enter__.
* `lsusb -v` now shows MC HCK as "Richcomm UPS emulator".

### DEMO

See *rcm_handle_data()* function that imitates power failure and battery discharge.

You can check your current UPS status with `upsc` (look for **ups.status**).

### UNLICENSE

This source code is in public domain. For more information, please refer to <http://unlicense.org/>
