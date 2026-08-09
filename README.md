# STM32 C++ Scalable Board Architecture & XIP Bootloader

This repository contains a modular, C++ based embedded software architecture designed for scalable product development across multiple custom PCBAs (Board Support Packages) and STM32 microcontroller families.

The architecture strictly decouples application logic from hardware specifics by utilizing a Custom Hardware Abstraction Layer (HAL) and an OS Abstraction Layer (OSAL). Furthermore, this project implements an advanced **Execute-in-Place (XIP) Architecture** enabling the main application to boot and execute directly from an external Octal-SPI flash memory chip via a customized two-stage bootloader.

## Core Technologies
*   **Language:** C++17 (for zero-cost abstractions and RAII)
*   **Build System:** CMake (Multi-target architecture, MinGW Makefiles generator)
*   **Execution Model:** FreeRTOS (Abstracted via OSAL)
*   **Networking:** lwIP (Abstracted via `NetManager` / `IEth`)
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

### 2. Applications (`apps/blinky/`, `apps/ethernetdev/`, `apps/template/`)
Applications are compiled to live at `0x70000000` (External Flash).
*   **Clock Inheritance Strategy:** Applications are compiled without the `-DBOOTLOADER` macro. This prevents the Application from attempting to re-initialize the locked PLLs or XSPI GPIO pins. Instead, the Application seamlessly inherits the 600MHz environment left by the bootloader and just updates its internal variables via `SystemCoreClockUpdate()`.

## Architecture Layers

The repository is structured to enforce strict dependency rules. Higher layers can only depend on lower layers.

*   `apps/`: **Application Layer.** Contains `main.cpp` entry points for different applications.
*   `boards/`: **Board Support Packages (BSPs).** PCBA-specific configurations. Each board defines its own oscillators, voltage regulators, pin muxing (`board_init.cpp`), linker scripts, and board resource accessors (e.g. `Board_GetLed()`).
*   `components/`: **Reusable Modules.** Hardware-independent business logic, algorithms, and RTOS Tasks (e.g. `BlinkTask`, `NetManager`).
*   `core/`:
    *   `osal/`: **OS Abstraction Layer.** C++ interfaces wrapping the FreeRTOS API (`Thread`, `Mutex`).
    *   `hal/`: **Custom HAL.** Pure virtual C++ interfaces for peripherals (`IGpio`, `IUart`, `IEth`, etc.).
*   `targets/`: **MCU Target Layer.** The implementation of the `core/hal/` using specific vendor SDKs. Contains MCU startup code, interrupt handlers, and lwIP port files.
*   `vendor/`: **Vendor SDK.** STM32Cube libraries, CMSIS, ST ExtMem Manager, FreeRTOS, and lwIP source code.

## Prerequisites

| Tool | Version | Notes |
|---|---|---|
| `arm-none-eabi-gcc` | 13.3+ | ARM GNU Toolchain |
| `cmake` | 3.20+ | |
| `mingw32-make` | Any | From [MSYS2](https://www.msys2.org/) (`ucrt64` environment) |

Ensure `arm-none-eabi-gcc` and `mingw32-make` are on your `PATH`.

## Building the Project

This project uses a multi-target CMake build strategy. You must specify the application to build via `-DAPP=<name>`.

### 1. Build the Bootloader

```bash
# Configure
cmake -G "MinGW Makefiles" -S . -B build -DAPP=bootloader

# Compile
cmake --build build
```

Output: `apps/bootloader/programming_files/bootloader_nucleo_h7s3l8.bin`

*Flash to `0x08000000` via STM32CubeProgrammer.*

### 2. Build an Application

#### Blinky (LED smoke test)
```bash
cmake -G "MinGW Makefiles" -S . -B build -DAPP=blinky
cmake --build build
```

Output: `apps/blinky/programming_files/blinky_nucleo_h7s3l8.bin`

#### Ethernet Dev
```bash
cmake -G "MinGW Makefiles" -S . -B build -DAPP=ethernetdev
cmake --build build
```

Output: `apps/ethernetdev/programming_files/ethernetdev_nucleo_h7s3l8.bin`

*Flash application binaries to `0x70000000` using the `MX25UW25645G_NUCLEO-H7S3L8` external loader in STM32CubeProgrammer.*

> **Artifact convention:** After each build, `.bin` and `.hex` files are automatically copied into `apps/<app>/programming_files/`. These files **are tracked by git** and pushed to the remote — so the latest flashable binary for each app is always available directly from the repository without needing to rebuild. Intermediate build artifacts (`build/`, `*.elf`, `*.map`) remain gitignored.

### Network Configuration

When building a non-bootloader application, network parameters can be overridden at configure time:

| CMake Variable | Default | Description |
|---|---|---|
| `NET_USE_DHCP` | `OFF` | Enable DHCP (falls back to static on timeout) |
| `NET_STATIC_IP_ADDR` | `192, 168, 1, 100` | Static/fallback IP (byte-comma format) |
| `NET_STATIC_NETMASK` | `255, 255, 255, 0` | Subnet mask |
| `NET_STATIC_GATEWAY` | `192, 168, 1, 1` | Gateway |
| `NET_NODE_ID` | `1` | Unique node ID (1–65535) |
| `NET_DHCP_TIMEOUT_MS` | `5000` | DHCP timeout before static fallback (ms) |

Example — enable DHCP with a custom node ID:
```bash
cmake -G "MinGW Makefiles" -S . -B build -DAPP=ethernetdev \
      -DNET_USE_DHCP=ON \
      -DNET_NODE_ID=42
cmake --build build
```

Fixed port assignments (in `cmake/net_config.h.in`):
- TCP command port: `4000`
- UDP data port: `5000`
- UDP discovery port: `3999`

### 3. Creating a New Application

A blank starting point exists in `apps/template/`. Copy it and register the new app name in the root `CMakeLists.txt` if it needs `BlinkTask` or other shared components.

## Porting to a New Board

The architecture is designed so that `main.cpp` requires **zero changes** when targeting a new board. All board-specific knowledge is encapsulated in `boards/<board>/`.

### Board Contract

Each board must implement two functions declared in `board_init.h`:

```cpp
// Performs all hardware init: MPU, cache, clocks, HAL.
void Board_Init();

// Returns the board's status LED, fully initialized (clock enabled, GPIO configured).
hal::IGpio& Board_GetLed();
```

`main.cpp` calls these and receives abstract interface references — it never touches a register or pin number.

### Steps to Add a New Board

1. **Create `boards/<new_board>/`** with:
   - `board_init.h` — copy the existing one verbatim (same API contract)
   - `board_init.cpp` — implement `Board_Init()` and `Board_GetLed()` for the new hardware
   - Linker scripts (`.ld`) for bootloader and application
   - `stm32<mcu>xx_hal_msp.c` — peripheral MSP init callbacks
   - `CMakeLists.txt` — add sources, include path, linker script selection

2. **If using a new MCU family**, create `targets/<new_target>/` with:
   - Concrete HAL implementations (`<Mcu>Gpio.cpp`, `<Mcu>Eth.cpp`, etc.)
   - Startup file and system init (`.s`, `.c`)
   - `FreeRTOSConfig.h` tuned for the target
   - `CMakeLists.txt` — compiler flags, CPU arch, HAL sources

3. **Build** by passing the new board and target:
   ```bash
   cmake -G "MinGW Makefiles" -S . -B build \
         -DAPP=ethernetdev \
         -DBOARD=<new_board> \
         -DTARGET=<new_target>
   cmake --build build
   ```

4. **`main.cpp` — unchanged ✅**

## C++ & FreeRTOS "Gotchas"
- **MSP Reset Bug:** On Cortex-M processors, FreeRTOS resets the Main Stack Pointer (MSP) when the scheduler starts. **NEVER** allocate hardware drivers or RTOS Tasks on the local `main()` stack. Always mark them as `static` or allocate them globally so they are placed in `.bss` and survive the scheduler boot sequence.
- **Linker Specs:** Always build with `--specs=nano.specs --specs=nosys.specs` to avoid linker errors for missing standard library syscalls (`_close`, `_read`). The resulting `_close is not implemented` linker warnings are expected and benign.
- **Binary Bloat:** Always compile with `-fno-exceptions -fno-rtti` in a bare-metal environment to prevent massive standard library bloat that can easily overflow internal FLASH.
- **lwIP + FreeRTOS only in applications:** `Stm32H7Eth.cpp`, `NetManager.cpp`, and `FreeRtosThread.cpp` are excluded from the bootloader build. The bootloader runs bare-metal with no RTOS or network stack.
