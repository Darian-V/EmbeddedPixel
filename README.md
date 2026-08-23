# EmbeddedPixel: Scalable C++ Embedded Architecture & Multi-Board BSP Framework

`EmbeddedPixel` is a modular, C++17 embedded software framework designed for scalable product development across custom PCBAs (Board Support Packages) and ARM Cortex-M microcontroller families.

The architecture strictly decouples application business logic from underlying hardware peripherals and operating systems through clean **Hardware Abstraction Layers (HAL)** and **OS Abstraction Layers (OSAL)**. It supports flexible execution paradigms, ranging from single-stage internal flash applications to high-performance **Two-Stage Execute-in-Place (XIP)** external memory systems.

---

## Documentation Index

| Document | Purpose |
|---|---|
| 📖 [**`docs/PROTOCOL_SPEC.md`**](docs/PROTOCOL_SPEC.md) | **Canonical Wire Protocol & Binary Formats**: Ports (`50000`/`50001`/`50002`), `PE_Header`, Discovery (`0x0001`/`0x0003`), FourCC Telemetry (`0x0200`), Unified CLI (`0x0150`), OTA (`0x0130`), `AppImageHeader` at `0x0200`, and error codes. |
| 💻 [**`docs/DESKTOP_INTEGRATION.md`**](docs/DESKTOP_INTEGRATION.md) | **Host Software & UI Integration Guide**: Copy-paste Python & JS/Electron client examples, serial & TCP CLI workflows, and OTA pre-flight safety gates. |
| 🔧 [**`docs/HARDWARE_BRINGUP_NOTES.md`**](docs/HARDWARE_BRINGUP_NOTES.md) | **Hardware Bring-Up & Lessons Learned**: Cortex-M7 D-Cache coherency, STM32H7RS HAL `pbuf` callbacks, AHB SRAM clock gating (`__HAL_RCC_SRAM1_CLK_ENABLE()`), and PHY diagnostics. |

---

## Core Technologies

* **Language:** C++17 (zero-cost abstractions, RAII, type safety)
* **Build System:** CMake + Ninja (or MinGW Makefiles)
* **Execution Model:** FreeRTOS (Abstracted via `core/osal/Thread.h`, `Mutex.h`, `Queue.h`)
* **Networking Stack:** lwIP with zero-copy DMA streaming (`NetManager`, `TelemetryService`, `OtaService`, `DiscoveryService`)
* **Time Synchronization:** Microsecond disciplined clock engine (`sys::TimeManager`, `hal::ITimeSource`, PI Slew disciplining)
* **Supported MCU Families:** STM32H7RS (Cortex-M7 @ 600MHz), STM32H743 (Cortex-M7 @ 480MHz), extensible to any Cortex-M
* **Reference Boards:** ST Nucleo-H7S3L8, PixelJam, Custom PCBAs

---

## Architecture Layers

```
apps/        -> Board-agnostic application logic & entry points (main.cpp)
boards/      -> Board Support Packages (BSPs: pin muxing, oscillators, power, memory controllers, Board_GetLed())
components/  -> Reusable business logic (SysController, CliEngine, NetManager, BlinkTask, TelemetryService, TimeManager)
core/        -> HAL pure virtual interfaces (IGpio, IUart, IEth, ITempSensor, ITimeSource) & FreeRTOS OSAL wrappers
targets/     -> Concrete MCU drivers, startup vectors, fault handlers, lwIP port files
vendor/      -> Vendor SDKs (CMSIS, STM32Cube, FreeRTOS, lwIP, ExtMem Manager)
```

Higher layers depend only on abstract lower-layer interfaces. An application in `apps/` never includes vendor SDK headers or manipulates hardware registers directly.

---

## Flexible Execution Models

The framework natively supports two memory execution architectures depending on the hardware capabilities of the target board:

```mermaid
graph TD
    subgraph Model A: Monolithic Internal Flash (e.g. PixelJam)
        M_VEC["Vector Table (0x08000000)"] --> M_APP["Application (.text, .rodata, .data)"]
    end

    subgraph Model B: Two-Stage External Memory XIP (e.g. Nucleo-H7S3L8)
        B_BL["1. Bootloader Springboard<br/>(Internal Flash 0x08000000)<br/>- Inits PLL Clocks & MPU<br/>- Inits Octal/Quad-SPI Controller<br/>- Checks OTA Staging RAM<br/>- Jumps to External Flash"]
        B_APP["2. XIP Application<br/>(External Flash 0x70000000)<br/>- Inherits Clocks & Memory Bus<br/>- Runs directly in-place<br/>- Includes AppImageHeader at 0x0200"]
        B_BL -->|Springboard Jump| B_APP
    end
```

1. **Model A: Monolithic Internal Flash** (e.g. `PixelJam` / `STM32H743`)
   - The application is compiled to execute directly from internal flash memory (`0x08000000`).
   - Self-contained single-stage binary with zero external memory dependencies.

2. **Model B: Two-Stage External Memory XIP** (e.g. `Nucleo-H7S3L8` / `STM32H7RS`)
   - **Stage 1 (Bootloader)**: Resides in internal flash (`0x08000000`). Configures CPU clocks, MPU regions, initializes the high-speed external memory bus (Octal-SPI / Quad-SPI), switches the peripheral into memory-mapped mode, and executes a clean handoff.
   - **Stage 2 (XIP Application)**: Compiled to execute in-place directly from external memory (`0x70000000`), seamlessly inheriting the initialized hardware environment.

---

## Quickstart

### Prerequisites

| Tool | Recommended Version | Purpose / Installation |
|---|---|---|
| `arm-none-eabi-gcc` | 13.3+ | ARM GNU Toolchain (`powershell -File .\scripts\setup_toolchain.ps1`) |
| `cmake` | 3.20+ | CMake build generator (`winget install Kitware.CMake`) |
| `ninja` | 1.12+ | High-speed parallel build engine (`winget install Ninja-build.Ninja`) |
| `python` | 3.10+ | OTA updater & test automation (`winget install Python.Python.3.12`) |
| `STM32CubeProg` | Latest | STM32 flasher & external loaders (STMicroelectronics) |

> [!TIP]
> Run the environment audit script to verify all prerequisites and connected probes:
> ```powershell
> powershell -ExecutionPolicy Bypass -File .\scripts\audit_tools.ps1
> ```

```bash
# Clone with shallow submodules
git clone --recurse-submodules --shallow-submodules https://github.com/Darian-V/EmbeddedPixel.git
```

### Parameterized Build System

Build any application for any board using Ninja and the CMake configuration matrix:

```bash
cmake -G "Ninja" -S . -B build \
      -DAPP=<application_name> \
      -DBOARD=<board_target> \
      -DTARGET=<mcu_family>

cmake --build build
```


#### Application & Board Matrix

| Application (`APP`) | Board (`BOARD`) | MCU Target (`TARGET`) | Execution Model | Description |
|---|---|---|---|---|
| `bootloader` | `nucleo_h7s3l8` | `stm32h7` | Internal Flash Springboard | Two-stage XSPI bootloader & OTA flasher |
| `ethernetdev` | `nucleo_h7s3l8` | `stm32h7` | External Octal-SPI XIP | Network stack, UDP streaming, TCP CLI & OTA |
| `blinky` | `nucleo_h7s3l8` | `stm32h7` | External Octal-SPI XIP | Basic RTOS LED blink smoke test |
| `temp_test` | `nucleo_h7s3l8` | `stm32h7` | External Octal-SPI XIP | On-Chip DTS temperature sensor test |
| `UARTDebug` | `PixelJam` | `stm32h743` | Internal Flash Monolithic | Standalone UART terminal debug application |
| `template` | *(Any Board)* | *(Any Target)* | *(Configurable)* | Clean starting point for new applications |

---

## Flashing & Programming

After each successful compilation, ready-to-flash `.bin` and `.hex` binaries are automatically staged in `boards/<board>/apps/<app>/programming_files/<app>_<board>.bin`.

### 1. Internal Flash Targets (Monolithic Apps or Bootloaders)
Flash directly to MCU internal flash (`0x08000000`):
```bash
STM32_Programmer_CLI.exe -c port=SWD -d boards/<board>/apps/<app>/programming_files/<app>_<board>.bin 0x08000000 -v -rst
```

*Example (Bootloader on Nucleo-H7S3L8):*
```bash
STM32_Programmer_CLI.exe -c port=SWD -d boards/nucleo_h7s3l8/apps/bootloader/programming_files/bootloader_nucleo_h7s3l8.bin 0x08000000 -v -rst
```

*Example (UARTDebug on PixelJam):*
```bash
STM32_Programmer_CLI.exe -c port=SWD -d boards/PixelJam/apps/UARTDebug/programming_files/UARTDebug_PixelJam.bin 0x08000000 -v -rst
```

### 2. External Flash Targets (XIP Applications)
Flash to external memory (`0x70000000`) using the board's external flash loader (`.stldr`):
```bash
STM32_Programmer_CLI.exe -c port=SWD -el bin/ExternalLoader/<loader>.stldr -d boards/<board>/apps/<app>/programming_files/<app>_<board>.bin 0x70000000 -v -rst
```

*Example (EthernetDev on Nucleo-H7S3L8):*
```bash
STM32_Programmer_CLI.exe -c port=SWD -el bin/ExternalLoader/MX25UW25645G_NUCLEO-H7S3L8.stldr -d boards/nucleo_h7s3l8/apps/ethernetdev/programming_files/ethernetdev_nucleo_h7s3l8.bin 0x70000000 -v -rst
```

---

## Unified Command Line Interface (CLI)

Nodes provide a unified command processor accessible over ST-Link Virtual COM Port (`115200 8N1`) and TCP Port `50002`:

```text
EmbeddedPixel> status
[STATUS] Node ID: 1 | Board: Nucleo-H7S3L8
[STATUS] App Version: v1.0.0 | Bootloader: v1.0.0
[STATUS] IP: 192.168.1.111 | MAC: 00:80:E1:01:00:01
[STATUS] Uptime: 42.5s | Core Temp: 38.2 C
[STATUS] Telemetry: STREAMING | Active Channels: [CNTR]
[STATUS] Features: ethernet, telemetry, dts, ota, dynrate, cli
```

For complete command references and client recipes, see [**`docs/DESKTOP_INTEGRATION.md`**](docs/DESKTOP_INTEGRATION.md).

---

## Ethernet OTA Firmware Updates

Nodes with networking capabilities support field updates streamed over TCP port `50002` into internal SRAM, verified with IEEE 802.3 CRC32, and programmed into external flash by the bootloader upon soft reset:

```bash
python scripts/ota_updater.py --ip 192.168.1.111 --bin boards/nucleo_h7s3l8/apps/ethernetdev/programming_files/ethernetdev_nucleo_h7s3l8.bin
```

---

## Board Support Package (BSP) Porting Contract

The architecture is designed so that application logic in `apps/` requires **zero modifications** when porting to a new PCBA. All hardware specifics are encapsulated within `boards/<new_board>/`:

```
boards/<new_board>/
├── board_init.h / .cpp       # Implements Board_Init(), Board_GetLed(), clock tree, pin muxing
├── STM32H7_FLASH.ld         # Linker script for internal or external memory layout
├── stm32h7xx_hal_msp.c      # Peripheral low-level MSP callbacks
└── CMakeLists.txt           # Board compiler flags, includes, and sources
```

### Board API Contract (`board_init.h`)
Each board must implement the standard contract functions:

```cpp
// Initializes MPU, D-Cache, oscillators, PLLs, and board power rails
void Board_Init();

// Returns the board's status LED, fully initialized
hal::IGpio& Board_GetLed();
```
