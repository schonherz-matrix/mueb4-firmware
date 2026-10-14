# mueb4-firmware

Firmware for 4th generation MUEB.

## Prerequisites

[STM32CubeIDE](https://www.st.com/en/development-tools/stm32cubeide.html) 1.19.0+

[Boost](https://www.boost.org/) 1.92.0+, by default outside the repository folder one level up. e.g. "../" or in any location (must be configured in c++ build settings)

## How to import the project

Open STM32CubeIDE.

Use File->Open projects from file system.

Specify "Import source", set to repository root directory(mueb4-firmware).

Select all, click Finish.

### Download ST drivers

Help->Configuration Tool->Manage embedded software packages.

Under **STM32F0**, select **STM32Cube MCU Package for STM32F0 Series** with the **1.11.6 version**, click install.

### Generating code

Under **mueb4-firmware-app** and **mueb4-firmware-boot** open the corresponding ***.ioc** file.

Project->Generate code(ALT+K)

#### Note

The code generator only generates/updates main.c and the peripheral files not the main.cc file.

## Important hardware information

### Microcontroller

https://www.st.com/en/microcontrollers-microprocessors/stm32f030c8.html

| Part No     | Reference                   | Package | Flash     | RAM      | IO   | Freq   |
| :---------- | --------------------------- | ------- | --------- | -------- | ---- | ------ |
| STM32F030C8 | STM32F030C8T6/STM32F030C8Tx | LQFP48  | 64 kBytes | 8 kBytes | 39   | 48 MHz |

### WIZnet w5500 ethernet chip

https://www.wiznet.io/product-item/w5500/

https://github.com/Wiznet/ioLibrary_Driver

### Microchip 24AA02E48T-I/OT

https://www.microchip.com/wwwproducts/en/24AA02E48

## Supported protocols

[sACN, E1.31 2018](https://tsp.esta.org/tsp/documents/docs/ANSI_E1-31-2018.pdf)

[Art-Net 4](https://art-net.org.uk/downloads/art-net.pdf), Art-Net™ Designed by and Copyright Artistic Licence Holdings Ltd.
