# STM32H7 Ethernet Development Lessons Learned

**Status Note:** The Ethernet Rx/Tx paths are completely functional. This document records the major roadblocks and architectural discoveries made during the bring-up phase.

## 1. STM32H7RS HAL `pbuf` Allocation Architecture
**Symptom:** The PHY negotiated perfectly, the link was up, and the transmit (Tx) path showed success, but the receive (Rx) path silently dropped all packets (Ping returned "Destination host unreachable").

**Root Cause:**
The STM32H7RS HAL departs from older STM32 families (like F4/F7). It is specifically designed to integrate directly with lwIP's `pbuf` structures. 
- In older HALs, `HAL_ETH_ReadData` returned raw buffer pointers (like an `ETH_RxPacketInfo` struct).
- In the STM32H7RS HAL, `HAL_ETH_ReadData` internally invokes `ETH_UpdateDescriptor` and expects the user application to provide memory by implementing weak callbacks (`HAL_ETH_RxAllocateCallback` and `HAL_ETH_RxLinkCallback`).

**Lesson:**
To correctly process incoming packets:
1. Implement `HAL_ETH_RxAllocateCallback` to provide the DMA with a raw buffer pointer.
2. Implement `HAL_ETH_RxLinkCallback` to allocate an lwIP `pbuf` (using `pbuf_alloc(PBUF_RAW, Length, PBUF_POOL)`), copy the DMA buffer payload into it, and link it to the existing `pbuf` chain.
3. Call `HAL_ETH_ReadData`. When it returns `HAL_OK`, the `pAppBuff` pointer will actually point to a fully formed `pbuf` chain ready to be passed directly to `netif.input()`.

## 2. Cortex-M7 D-Cache and DMA Coherency
**Symptom:** Transmitted packets contained stale data or were completely ignored by the hardware, and the CPU read stale DMA descriptor `OWN` bits, causing polling loops to hang.

**Root Cause:**
The Cortex-M7 has a very aggressive Data Cache (D-Cache). The Ethernet MAC uses DMA to read from Tx descriptors and write to Rx descriptors directly in SRAM. If the CPU creates a packet in cacheable memory, the DMA will read the stale old data directly from physical RAM.

**Lesson:**
- **MPU Configuration:** The memory region used for DMA descriptors and buffers (e.g., `0x30000000`) must be configured via the MPU as Non-Cacheable, Non-Bufferable, and Shareable.
- **Cache Flushing (The "Nuclear" Option):** Even with the MPU configured, startup code (like zero-initializing BSS) might populate the D-Cache *before* the MPU is enabled. Always run `SCB_CleanInvalidateDCache()` immediately after enabling the MPU.
- **Explicit Cache Maintenance:** To guarantee coherency and protect against MPU misconfigurations, always explicitly manage cache lines:
  - Clean Tx buffers and Tx descriptors to RAM before issuing a Transmit.
  - Invalidate Rx descriptors from Cache before reading them, so the CPU sees the updated `OWN` bit set by the DMA.

## 3. Hardware Checksum Offloading Interference
**Symptom:** The STM32 failed to transmit UDP broadcast packets (like DHCP Discover) even when Tx DMA completed successfully.

**Root Cause:**
The Ethernet MAC has an internal IPv4 checksum offloading engine. If enabled, it attempts to parse the payload to calculate the checksum. If lwIP generates a packet that doesn't perfectly conform to what the MAC expects (e.g., certain broadcast/multicast packets or missing IPv4 headers), the MAC may silently drop the packet rather than transmitting it.

**Lesson:**
When bringing up an Ethernet port with lwIP, **disable hardware checksum offloading** (`ETH_TX_PACKETS_FEATURES_CSUM`) initially. Let lwIP calculate all checksums in software (`CHECKSUM_GEN_IP = 1`, etc.). Only enable hardware offloading as an optimization once standard networking (Ping, DHCP) is proven stable.

## 4. PHY Register Diagnostics
**Symptom:** Ambiguity on whether the issue was in software (lwIP/HAL) or physical hardware (cable, RMII clock, PHY).

**Root Cause:**
Relying solely on "Link UP" (Basic Status Register Bit 2) is insufficient, as it does not guarantee that the 50MHz RMII reference clock is running properly or that speeds match.

**Lesson:**
Always write a PHY diagnostic function that dumps all 32 PHY registers (especially Register 0, 1, 4, 5, and 31 for LAN8742) over the serial console. 
- A successful 100M Full-Duplex negotiation is confirmed if Auto-Negotiation is Complete (Reg 1, Bit 5 is 1) and the PHY Specific Status Register (Reg 31) reflects `110` in the speed indication bits.
- This immediately rules out hardware/clocking faults and allows you to focus strictly on DMA/Cache/Software issues.

## 5. AHB SRAM Clock Initialization
**Symptom:** The CPU wrote to the DMA descriptors to hand them to the DMA, but the DMA immediately returned an `RBU` (Receive Buffer Unavailable) error. The `HAL_ETH_RxAllocateCallback` was called in an infinite loop, burning through buffers without any success.

**Root Cause:**
On the STM32H7RS, the Ethernet Rx/Tx buffers and DMA descriptors are mapped to `SRAMAHB` (`SRAM1` at `0x30000000`). Unlike some older STM32 families where SRAM is universally clocked by default, the STM32H7RS explicitly gates the clock to `SRAM1` to save power. If the clock is disabled (`RCC_AHB2ENR_SRAM1EN == 0`), all CPU writes to `0x30000000` are completely ignored by the bus, and reads return `0x00000000`. This causes the HAL to read an empty descriptor, allocate a buffer, attempt to write it to memory (which is ignored), and loop infinitely.

**Lesson:**
Before configuring the MPU or initializing the MAC/DMA, you must explicitly enable the clock to the specific SRAM bank where your descriptors and buffers reside:
```cpp
__HAL_RCC_SRAM1_CLK_ENABLE();
```
Without this single line, the entire Ethernet subsystem will fail silently, appearing as a cache or memory mapping issue.
