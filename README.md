# STM32 C++ Scalable Board Architecture

This repository contains a modular, C++ based embedded software architecture designed for scalable product development across multiple custom PCBAs (Board Support Packages) and STM32 microcontroller families. 

The architecture strictly decouples application logic from hardware specifics by utilizing a Custom Hardware Abstraction Layer (HAL) and an OS Abstraction Layer (OSAL).

## Core Technologies
*   **Language:** C++17 (for zero-cost abstractions and RAII)
*   **Build System:** CMake (Multi-target architecture)
*   **Execution Model:** FreeRTOS (Abstracted via OSAL)
*   **Supported MCUs:** STM32H7 (Easily extensible)
*   **Supported Boards:** Nucleo-H7S3L8, Custom PCBAs

## Architecture Layers

The repository is structured to enforce strict dependency rules. Higher layers can only depend on lower layers.

*   `apps/`: **Application Layer.** Contains `main.cpp` entry points for different applications (e.g., `blinky`, `motor_control`). Instantiates hardware objects and passes them to RTOS tasks.
*   `boards/`: **Board Support Packages (BSPs).** PCBA-specific configurations. Each board defines its own oscillators, voltage regulators, pin muxing (`board_init.cpp`), and linker scripts.
*   `components/`: **Reusable Modules.** Hardware-independent business logic, algorithms, and RTOS Tasks (e.g. `BlinkTask`).
*   `core/`: 
    *   `osal/`: **OS Abstraction Layer.** C++ interfaces wrapping the FreeRTOS API (`Thread`, `Mutex`).
    *   `hal/`: **Custom HAL.** Pure virtual C++ interfaces for peripherals (`IGpio`, `IUart`, etc.).
*   `targets/`: **MCU Target Layer.** The implementation of the `core/hal/` using specific vendor SDKs (e.g., `stm32h7`). Contains MCU startup code and interrupt handlers.
*   `vendor/`: **Vendor SDK.** STM32Cube libraries, CMSIS, and FreeRTOS source code.

## Building the Project

This project uses a multi-target CMake build strategy. You must specify the Application, Board, and Target MCU during CMake configuration so that the build system knows which layers to link.

### Building Blinky for Nucleo-H7S3L8
```bash
# Generate build files
cmake -B build -DAPP=blinky -DBOARD=nucleo_h7s3l8 -DTARGET=stm32h7 -G "MinGW Makefiles"

# Compile the project
cmake --build build
```

## Adding a New Custom PCBA
1. Create a new directory under `boards/` (e.g., `boards/my_custom_pcba`).
2. Provide a `board_init.cpp` that implements `void Board_Init()` with your specific clock tree and pin muxing.
3. Provide your PCBA's specific `.ld` linker script.
4. Create a `CMakeLists.txt` mapping these files.
5. Build using `-DBOARD=my_custom_pcba`.

## C++ & FreeRTOS "Gotchas"
- **MSP Reset Bug:** On Cortex-M processors, FreeRTOS resets the Main Stack Pointer (MSP) when the scheduler starts. **NEVER** allocate hardware drivers or RTOS Tasks on the local `main()` stack. Always mark them as `static` or allocate them globally so they are placed in `.bss` and survive the scheduler boot sequence.
- **Linker Specs:** Always build with `--specs=nano.specs --specs=nosys.specs` to avoid linker errors for missing standard library syscalls (`_close`, `_read`).
- **Binary Bloat:** Always compile with `-fno-exceptions -fno-rtti` in a bare-metal environment to prevent massive standard library bloat that can easily overflow internal FLASH.
