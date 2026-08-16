# Ethernet Development Handoff

## Current Status
We are currently developing the bare-metal Ethernet driver and lwIP port for the Nucleo-H7S3L8 board. 
- **PHY & Link**: The LAN8742 PHY successfully auto-negotiates to 100M Full-Duplex. The physical link is confirmed healthy.
- **Transmit (Tx) Path**: The DMA successfully completes transmissions and packets are routed correctly.
- **Receive (Rx) Path**: **FULLY FUNCTIONAL**. The board successfully processes incoming packets and LwIP correctly responds to Pings (e.g. `192.168.1.111`).

## Recent Changes (SUCCESSFUL)
In the latest commit, we resolved the final missing piece of the Ethernet Rx path puzzle. 
- The STM32H7RS has `SRAMAHB` (`SRAM1` at `0x30000000`) clock-gated by default to save power. 
- We added `__HAL_RCC_SRAM1_CLK_ENABLE()` to `Stm32H7Eth::Init()`.
- Previously, the disabled clock caused the CPU writes (specifically assigning the `OWN=1` bit and buffer addresses to the DMA descriptors) to be silently dropped by the bus matrix. The DMA hardware, seeing `OWN=0`, threw a continuous `RBU` (Receive Buffer Unavailable) error and LwIP looped infinitely trying to allocate new buffers for the failed descriptors.
- With the clock enabled, the CPU descriptor writes now persist in SRAM, the DMA correctly receives packets, and the ping responds flawlessly.
- We also cleaned up the debug logging now that the driver is stable.

## Next Steps
- Implement higher-level networking protocols (e.g. DHCP, TCP/UDP sockets) using the fully functional LwIP stack.
- Review LwIP configuration (`lwipopts.h`) to optimize memory and performance.
- Follow the node-to-host protocol and streaming architecture defined in [ethernet_communication_architecture.md](file:///d:/repos/EmbeddedPixel/docs/ethernet_communication_architecture.md).
