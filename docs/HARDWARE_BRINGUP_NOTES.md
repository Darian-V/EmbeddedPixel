# STM32H7 Hardware Bring-Up Notes & Architectural Lessons

**Target Series:** STM32H7RS, STM32H743  
**Components:** Cortex-M7 Core, Ethernet MAC (RMII), LAN8742 PHY, Octal-SPI External Flash, FreeRTOS, lwIP  

This document records the critical hardware quirks, memory architecture gotchas, and solutions discovered during the bring-up and stabilization of the STM32H7 platform.

---

## 1. STM32H7RS HAL `pbuf` Integration Architecture

### Symptom
PHY negotiated 100M Full-Duplex and the link was reported UP. Transmit (Tx) DMA succeeded, but Receive (Rx) silently dropped all packets (Ping returned *"Destination host unreachable"*).

### Root Cause
The STM32H7RS HAL departs from older STM32 families (F4/F7/classic H7):
- Older HALs returned raw buffer pointers (e.g. `ETH_RxPacketInfo`).
- The STM32H7RS HAL interacts directly with lwIP `pbuf` structures. `HAL_ETH_ReadData` internally invokes `ETH_UpdateDescriptor` and expects application callbacks (`HAL_ETH_RxAllocateCallback` and `HAL_ETH_RxLinkCallback`).

### Solution
1. Implement `HAL_ETH_RxAllocateCallback` to supply raw DMA buffer pointers.
2. Implement `HAL_ETH_RxLinkCallback` to allocate an lwIP `pbuf` (`pbuf_alloc(PBUF_RAW, Length, PBUF_POOL)`), copy the DMA payload, and link to the `pbuf` chain.
3. Call `HAL_ETH_ReadData`. When it returns `HAL_OK`, pass the resulting `pbuf` directly to `netif.input()`.

---

## 2. Cortex-M7 D-Cache & DMA Coherency

### Symptom
Transmitted Ethernet packets contained stale data, and CPU read stale DMA descriptor `OWN` bits, causing polling loops to hang.

### Root Cause
The Cortex-M7 utilizes an aggressive L1 Data Cache (D-Cache). The Ethernet MAC uses DMA to access descriptors and packet buffers in SRAM. If buffers are in cacheable memory, the DMA accesses stale physical RAM while the CPU sees cached values.

### Solution
- **MPU Configuration**: Place DMA descriptors and buffers in `SRAMAHB` (`0x30000000`) and configure via the MPU as **Non-Cacheable, Non-Bufferable, and Shareable**.
- **Startup Invalidation**: Startup code (e.g. zero-initializing `.bss`) may populate cache lines *before* MPU is activated. Always call `SCB_CleanInvalidateDCache()` immediately after configuring the MPU.
- **Explicit Cache Maintenance**:
  - Clean Tx buffers to RAM prior to initiating DMA transmission (`SCB_CleanDCache_by_Addr`).
  - Invalidate Rx descriptors before reading (`SCB_InvalidateDCache_by_Addr`).

---

## 3. AHB SRAM Clock Gating Gotcha

### Symptom
CPU wrote to DMA descriptors to hand them to hardware (`OWN=1`), but the DMA immediately asserted `RBU` (Receive Buffer Unavailable). The allocation callback looped infinitely.

### Root Cause
On STM32H7RS, Ethernet descriptors and buffers reside in `SRAMAHB` (`SRAM1` at `0x30000000`). Unlike older families where SRAM banks are enabled by default, the STM32H7RS explicitly gates the clock to `SRAM1` to conserve power. If the clock is disabled (`RCC_AHB2ENR_SRAM1EN == 0`), CPU writes to `0x30000000` are dropped by the bus matrix, and reads return `0x00000000`.

### Solution
Explicitly enable the clock to `SRAM1` prior to descriptor initialization:
```cpp
__HAL_RCC_SRAM1_CLK_ENABLE();
```

---

## 4. Hardware Checksum Offloading Interference

### Symptom
STM32 failed to transmit UDP broadcast packets (like DHCP Discover) even when Tx DMA completed without errors.

### Root Cause
The Ethernet MAC has an internal hardware IPv4 checksum offload engine. When enabled, if an outgoing packet does not conform precisely to what the MAC expects (e.g. certain broadcast frames or missing headers), the hardware MAC engine silently drops the frame.

### Solution
Disable hardware checksum offloading (`ETH_TX_PACKETS_FEATURES_CSUM`) during driver bring-up and allow lwIP to calculate checksums in software (`CHECKSUM_GEN_IP = 1`). Re-enable hardware offloading only after basic networking (Ping, DHCP) is verified stable.

---

## 5. LAN8742 PHY Diagnostics

### Diagnostic Tip
Relying solely on "Link UP" (Basic Status Register Bit 2) does not verify that the 50 MHz RMII reference clock is stable or that auto-negotiation parameters match.

Implement a diagnostic helper to dump PHY registers 0, 1, 4, 5, and 31:
- **Auto-Negotiation Complete**: Register 1, Bit 5 == `1`.
- **100M Full-Duplex Verification**: LAN8742 Register 31 (Special Control/Status) bits `[4:2]` must read `110` (`100Base-TX Full-Duplex`).
