---
name: stm32-flash-and-debug
description: >-
  Use this skill to flash firmware (bootloader or XIP applications) to STM32 target
  boards (e.g. Nucleo-H7S3L8, PixelJam) and verify live execution via serial COM port monitoring.
---

# STM32 Flashing and Hardware Serial Debugging Runbook

This skill defines the exact workflow and commands for programming target hardware and verifying firmware execution over ST-Link Virtual COM ports with full debug verbosity (`NET_LOG_LEVEL=3`).

---

## 1. Tooling Locations

* **STM32CubeProgrammer CLI**:
  `D:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe`
* **External Loaders Directory**:
  `D:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\ExternalLoader\`
  * **Nucleo-H7S3L8 XSPI Flash**: `MX25UW25645G_NUCLEO-H7S3L8.stldr`

---

## 2. Build Configuration (Debug Level 3)

Ensure build is configured with `NET_LOG_LEVEL=3` for verbose network/system diagnostic telemetry:

```powershell
# Example: Configure & Build ethernetdev on nucleo_h7s3l8
cmake -G "MinGW Makefiles" -S . -B build -DAPP=ethernetdev -DBOARD=nucleo_h7s3l8 -DTARGET=stm32h7
cmake --build build
```

The build post-action automatically stages `.bin` and `.hex` files to:
`boards/<BOARD>/apps/<APP>/programming_files/<APP>_<BOARD>.bin`

---

## 3. Flashing Procedures

### A. Nucleo-H7S3L8 (Two-Stage Bootloader + External Flash XIP)

1. **Flash Bootloader** (Internal Flash at `0x08000000`):
   ```powershell
   & "D:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -c port=SWD -d "boards/nucleo_h7s3l8/apps/bootloader/programming_files/bootloader_nucleo_h7s3l8.bin" 0x08000000 -v -rst
   ```

2. **Flash Application** (External Octal-SPI Flash at `0x70000000`):
   ```powershell
   & "D:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -c port=SWD -el "D:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\ExternalLoader\MX25UW25645G_NUCLEO-H7S3L8.stldr" -d "boards/nucleo_h7s3l8/apps/<APP>/programming_files/<APP>_nucleo_h7s3l8.bin" 0x70000000 -v -rst
   ```

### B. Standard Internal Flash Boards (e.g. PixelJam / STM32H743)

```powershell
& "D:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -c port=SWD -d "boards/<BOARD>/apps/<APP>/programming_files/<APP>_<BOARD>.bin" 0x08000000 -v -rst
```

---

## 4. Live Serial Verification (MCP Serial Tools)

1. **Discover COM Port**:
   * Call MCP tool `serial:list_ports` to find the STLink Virtual COM Port (e.g., `COM3`).
2. **Open Port**:
   * Call MCP tool `serial:open_port` with `port: "COM3"` and `baudrate: 115200`.
3. **Capture Logs**:
   * Call MCP tool `serial:read_serial` with `max_lines: 50` and `timeout_sec: 5` to `8`.
   * Verify bootloader handoff (`XSPI2 Initialized`, `External Flash mapped to 0x70000000`, `Jumping to Application...`).
   * Verify application initialization, PHY link detection, DHCP / Static IP assignment, and FreeRTOS task telemetry.
4. **Close Port**:
   * Call MCP tool `serial:close_port` when verification is complete.
