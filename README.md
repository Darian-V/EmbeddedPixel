# STM32 C++ Scalable Board Architecture & XIP Bootloader

This repository contains a modular, C++ based embedded software architecture designed for scalable product development across multiple custom PCBAs (Board Support Packages) and STM32 microcontroller families.

The architecture strictly decouples application logic from hardware specifics by utilizing a Custom Hardware Abstraction Layer (HAL) and an OS Abstraction Layer (OSAL). Furthermore, this project implements an advanced **Execute-in-Place (XIP) Architecture** enabling the main application to boot and execute directly from an external Octal-SPI flash memory chip via a customized two-stage bootloader.

## Core Technologies
*   **Language:** C++17 (for zero-cost abstractions and RAII)
*   **Build System:** CMake (Multi-target architecture, MinGW Makefiles generator)
*   **Execution Model:** FreeRTOS (Abstracted via OSAL)
*   **Networking:** lwIP (Abstracted via `NetManager` / `IEth` / `IPhy`)
*   **Supported MCUs:** STM32H7RS, STM32H743
*   **Supported Boards:** Nucleo-H7S3L8, PixelJam, Custom PCBAs
*   **Memory Execution:** Internal Flash (Bootloader / MCU Apps) + Octal-SPI External Flash (Application XIP)

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
*   `components/`: **Reusable Modules.** Hardware-independent business logic, algorithms, RTOS Tasks (`BlinkTask`), and runtime network orchestration (`NetManager`).
*   `core/`:
    *   `osal/`: **OS Abstraction Layer.** C++ interfaces wrapping the FreeRTOS API (`Thread`, `Mutex`).
    *   `hal/`: **Custom HAL.** Pure virtual C++ interfaces for peripherals (`IGpio`, `IUart`, `IEth`, `IPhy`, etc.).
*   `targets/`: **MCU Target Layer.** The implementation of the `core/hal/` using specific vendor SDKs. Contains MCU startup code, PHY abstraction drivers (`Lan8742Phy`), Cortex-M7 diagnostic fault handlers, interrupt handlers, and lwIP port files.
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

Output: `boards/nucleo_h7s3l8/apps/bootloader/programming_files/bootloader_nucleo_h7s3l8.bin`

### 2. Build an Application

#### Blinky (LED smoke test on Nucleo-H7S3L8)
```bash
cmake -G "MinGW Makefiles" -S . -B build -DAPP=blinky -DBOARD=nucleo_h7s3l8 -DTARGET=stm32h7
cmake --build build
```

#### Ethernet Dev (Network Stack on Nucleo-H7S3L8)
```bash
cmake -G "MinGW Makefiles" -S . -B build -DAPP=ethernetdev -DBOARD=nucleo_h7s3l8 -DTARGET=stm32h7
cmake --build build
```

#### UART Debug (PixelJam STM32H743)
```bash
cmake -G "MinGW Makefiles" -S . -B build_pj -DAPP=UARTDebug -DBOARD=PixelJam -DTARGET=stm32h743
cmake --build build_pj
```

### 3. Flashing via CLI (STM32CubeProgrammer)

Flash firmware binaries using the `STM32_Programmer_CLI.exe` tool over SWD:

#### A. Nucleo-H7S3L8 (Two-Stage Bootloader + External Flash XIP)

1. **Flash Bootloader** (Internal Flash at `0x08000000`):
   ```bash
   STM32_Programmer_CLI.exe -c port=SWD -d boards/nucleo_h7s3l8/apps/bootloader/programming_files/bootloader_nucleo_h7s3l8.bin 0x08000000 -v -rst
   ```

2. **Flash Application** (External Octal-SPI Flash at `0x70000000`):
   ```bash
   STM32_Programmer_CLI.exe -c port=SWD -el bin/ExternalLoader/MX25UW25645G_NUCLEO-H7S3L8.stldr -d boards/nucleo_h7s3l8/apps/ethernetdev/programming_files/ethernetdev_nucleo_h7s3l8.bin 0x70000000 -v -rst
   ```

#### B. Internal Flash Boards (e.g. PixelJam / STM32H743)

```bash
STM32_Programmer_CLI.exe -c port=SWD -d boards/PixelJam/apps/UARTDebug/programming_files/UARTDebug_PixelJam.bin 0x08000000 -v -rst
```

> **Artifact convention:** After each build, `.bin` and `.hex` files are automatically copied into `boards/<board>/apps/<app>/programming_files/`. These files **are tracked by git** and pushed to the remote — so the latest flashable binary for each app is always available directly from the repository without needing to rebuild. Intermediate build artifacts (`build/`, `*.elf`, `*.map`) remain gitignored.

### 4. Serial Verification & Telemetry

Open the ST-Link Virtual COM Port (`115200` baud, 8N1) using any terminal emulator (or serial MCP tools) to view live bootloader handoff and application diagnostics:

```text
=== Bootloader Started ===
XSPI2 Initialized.
EXTMEM_Init status: 0
External Flash mapped to 0x70000000.
Jumping to Application...

=== EthernetDev ===
[NET] NetManager: starting
[NET] LAN8742: Init OK (PHY addr 0)
[NET] NetManager: PHY ID = 0x0007C131
[NET] NetManager: link UP
[NET] NetManager: DHCP IP = 192.168.1.111
[NET DBG] ETH Rx 566 bytes
```

### Network Architecture & Configuration

Networking is managed at runtime by `NetManager`, which accepts an `IpConfig` structure supporting:
- **`net::IpMode::DHCP`**: Pure dynamic IP assignment via DHCP.
- **`net::IpMode::DHCP_WITH_FALLBACK`**: Dynamic IP assignment with automatic fallback to a static IP if the DHCP server does not acknowledge within a configurable timeout (`dhcp_timeout_ms`).
- **`net::IpMode::STATIC`**: Pure static IP configuration.

```cpp
static net::IpConfig ipCfg;
ipCfg.mode            = net::IpMode::DHCP_WITH_FALLBACK;
ipCfg.static_ip       = net::IP4_MAKE(192, 168, 1, 111);
ipCfg.netmask         = net::IP4_MAKE(255, 255, 255, 0);
ipCfg.gateway         = net::IP4_MAKE(192, 168, 1, 1);
ipCfg.dhcp_timeout_ms = 10000;
ipCfg.hostname        = "embeddedpixel";

static net::NetManager netMan(ethDriver, ipCfg);
```

Build-time network defaults can also be passed via CMake:

| CMake Variable | Default | Description |
|---|---|---|
| `NET_LOG_LEVEL` | `3` | Net log verbosity: `0`=off, `1`=err, `2`=info, `3`=debug |
| `NET_USE_DHCP` | `OFF` | Enable DHCP default |
| `NET_STATIC_IP_ADDR` | `192, 168, 1, 100` | Static/fallback IP (byte-comma format) |
| `NET_STATIC_NETMASK` | `255, 255, 255, 0` | Subnet mask |
| `NET_STATIC_GATEWAY` | `192, 168, 1, 1` | Gateway |
| `NET_NODE_ID` | `1` | Unique node ID (1–65535) |
| `NET_DHCP_TIMEOUT_MS` | `5000` | DHCP timeout before static fallback (ms) |

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
   - Concrete HAL implementations (`<Mcu>Gpio.cpp`, `<Mcu>Eth.cpp`, `<Phy>Phy.cpp`, etc.)
   - Startup file and system init (`.s`, `.c`)
   - Hardware diagnostic fault handlers (`fault_handler.cpp`)
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
- **ARM Cortex-M7 Memory Alignment (`MEM_ALIGNMENT 4`):** In lwIP on ARM Cortex-M7 processors, `#define MEM_ALIGNMENT 4` MUST be set in `lwipopts.h`. Without 4-byte memory alignment, lwIP heap allocations return 2-byte aligned addresses, which trigger an immediate Cortex-M7 Unaligned Access HardFault (`SCB->CFSR = 0x01000000`) when executing 64-bit `STRD`/`LDRD` instructions.
- **MSP Reset Bug:** On Cortex-M processors, FreeRTOS resets the Main Stack Pointer (MSP) when the scheduler starts. **NEVER** allocate hardware drivers or RTOS Tasks on the local `main()` stack. Always mark them as `static` or allocate them globally so they are placed in `.bss` and survive the scheduler boot sequence.
- **Linker Specs:** Always build with `--specs=nano.specs --specs=nosys.specs` to avoid linker errors for missing standard library syscalls (`_close`, `_read`). The resulting `_close is not implemented` linker warnings are expected and benign.
- **Binary Bloat:** Always compile with `-fno-exceptions -fno-rtti` in a bare-metal environment to prevent massive standard library bloat that can easily overflow internal FLASH.
- **lwIP + FreeRTOS only in applications:** `Stm32H7Eth.cpp`, `NetManager.cpp`, and `FreeRtosThread.cpp` are excluded from the bootloader build. The bootloader runs bare-metal with no RTOS or network stack.
