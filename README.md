# STM32 C++ Scalable Board Architecture & XIP Bootloader

This repository contains a modular, C++ based embedded software architecture designed for scalable product development across multiple custom PCBAs (Board Support Packages) and STM32 microcontroller families. 

The architecture strictly decouples application logic from hardware specifics by utilizing a Custom Hardware Abstraction Layer (HAL) and an OS Abstraction Layer (OSAL). Furthermore, this project implements an advanced **Execute-in-Place (XIP) Architecture** enabling the main application to boot and execute directly from an external Octal-SPI flash memory chip via a customized two-stage bootloader.

## Core Technologies
*   **Language:** C++17 (for zero-cost abstractions and RAII)
*   **Build System:** CMake (Multi-target architecture and Ninja generation)
*   **Execution Model:** FreeRTOS (Abstracted via OSAL)
*   **Supported MCUs:** STM32H7 (Specifically STM32H7RS)
*   **Supported Boards:** Nucleo-H7S3L8, Custom PCBAs
*   **Memory Execution:** Internal Flash (Bootloader) + Octal-SPI External Flash (Application XIP)

## Two-Stage XIP Bootloader Architecture

This repository heavily leverages the XSPI (Octal-SPI) peripherals of the STM32H7RS to run large applications entirely from external memory. 

### 1. Bootloader (`apps/bootloader/`)
Lives in the MCU's internal flash memory (`0x08000000`). It is responsible for serving as the hardware springboard:
*   **Clock Injection:** Turbocharges the CPU to 600MHz (via PLL1) and initializes PLL2 for the XSPI peripheral.
*   **Memory Protection Unit (MPU):** Configures the Cortex-M7 MPU to properly handle the external flash region (`0x70000000`).
*   **Hardware Mapping:** Probes the external flash chip via the ST ExtMem Manager, switches the chip to 8-line Octal Mode, and issues the `EXTMEM_MemoryMappedMode` API to switch the MCU's XSPI peripheral into a transparent memory-mapped mode.
*   **Handoff:** Carefully flushes and disables CPU Caches without issuing a generic `HAL_DeInit` (which would kill the XSPI memory bus), reads the Vector Table from `0x70000000`, and jumps execution!

### 2. Applications (`apps/blinky/`, `apps/template/`)
Applications are compiled to live at `0x70000000` (External Flash).
*   **Clock Inheritance Strategy:** Applications are compiled without the `-DBOOTLOADER` macro. This prevents the Application from attempting to re-initialize the locked PLLs or XSPI GPIO pins. Instead, the Application seamlessly inherits the 600MHz environment left by the bootloader and just updates its internal variables via `SystemCoreClockUpdate()`.

## Architecture Layers

The repository is structured to enforce strict dependency rules. Higher layers can only depend on lower layers.

*   `apps/`: **Application Layer.** Contains `main.cpp` entry points for different applications.
*   `boards/`: **Board Support Packages (BSPs).** PCBA-specific configurations. Each board defines its own oscillators, voltage regulators, pin muxing (`board_init.cpp`), and linker scripts for both the bootloader and the application.
*   `components/`: **Reusable Modules.** Hardware-independent business logic, algorithms, and RTOS Tasks (e.g. `BlinkTask`).
*   `core/`: 
    *   `osal/`: **OS Abstraction Layer.** C++ interfaces wrapping the FreeRTOS API (`Thread`, `Mutex`).
    *   `hal/`: **Custom HAL.** Pure virtual C++ interfaces for peripherals (`IGpio`, `IUart`, etc.).
*   `targets/`: **MCU Target Layer.** The implementation of the `core/hal/` using specific vendor SDKs. Contains MCU startup code and interrupt handlers.
*   `vendor/`: **Vendor SDK.** STM32Cube libraries, CMSIS, ST ExtMem Manager, and FreeRTOS source code.

## Building the Project

This project uses a multi-target CMake build strategy. You must specify the Application to build.

### 1. Build the Bootloader
```bash
# Generate build files
cmake -G Ninja -S . -B build/bootloader -DAPP=bootloader

# Compile the project
cmake --build build/bootloader
```
*Flash `bootloader_nucleo_h7s3l8.bin` to `0x08000000` via STM32CubeProgrammer.*

### 2. Build the Application (e.g., Blinky)
```bash
# Generate build files
cmake -G Ninja -S . -B build/blinky -DAPP=blinky

# Compile the project
cmake --build build/blinky
```
*Flash `blinky_nucleo_h7s3l8.bin` to `0x70000000` using the `MX25UW25645G_NUCLEO-H7S3L8` external loader.*

### 3. Creating a New Application
A blank starting point exists in `apps/template/`. You can copy it or configure it to start building your own application logic directly on the external memory.

## C++ & FreeRTOS "Gotchas"
- **MSP Reset Bug:** On Cortex-M processors, FreeRTOS resets the Main Stack Pointer (MSP) when the scheduler starts. **NEVER** allocate hardware drivers or RTOS Tasks on the local `main()` stack. Always mark them as `static` or allocate them globally so they are placed in `.bss` and survive the scheduler boot sequence.
- **Linker Specs:** Always build with `--specs=nano.specs --specs=nosys.specs` to avoid linker errors for missing standard library syscalls (`_close`, `_read`).
- **Binary Bloat:** Always compile with `-fno-exceptions -fno-rtti` in a bare-metal environment to prevent massive standard library bloat that can easily overflow internal FLASH.
