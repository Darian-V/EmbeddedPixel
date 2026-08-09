# Ethernet Development Handoff

## Current Status
We are currently developing the bare-metal Ethernet driver and lwIP port for the Nucleo-H7S3L8 board. 
- **PHY & Link**: The LAN8742 PHY successfully auto-negotiates to 100M Full-Duplex. The physical link is confirmed healthy.
- **Transmit (Tx) Path**: The DMA successfully completes transmissions (we see `ETH: Tx done` or similar), though it is unverified if the packets reach the network properly due to the Rx failure.
- **Receive (Rx) Path**: **CURRENTLY BROKEN**. The board drops or fails to process incoming packets. Pinging the board (`192.168.1.100`) from a PC results in "Destination host unreachable".

## Recent Changes (Unverified / Failing)
In the latest commit, we completely rewrote `Stm32H7Eth::ProcessRx()` and added the ST HAL expected weak callbacks (`HAL_ETH_RxAllocateCallback` and `HAL_ETH_RxLinkCallback`). 
We also added a "nuclear option" for D-Cache coherency (`SCB_CleanInvalidateDCache()` right after MPU enable). 
*Note: This latest code state is pushed to the `feature/ethernet` branch but the Rx path remains non-functional.*

## Next Steps for Debugging Rx
1. **Verify DMA Descriptor Ownership**: Check if the DMA is actually setting the `OWN` bit back to the CPU after a packet arrives. You can pause the debugger and inspect the memory at `0x30000000` (where `DMARxDscrTab` lives) to see if the descriptors change when a ping is sent.
2. **Verify MAC Counters**: Inspect the Ethernet MAC registers (specifically the MMC Receive Counters) via the debugger to see if the MAC is actually receiving broadcast ARP packets from the PHY. If the MAC counters do not increment, the issue is between the PHY and the MAC (e.g., RMII clocking configuration `ETH_RMII_REF_CLK`).
3. **RMII Clock Configuration**: Double check `RCC_ETH1REFCLKSOURCE_PHY` vs `RCC_ETH1PHYCLKSOURCE_PLL3S`. Ensure the Nucleo board hardware expects the STM32 to provide the 50MHz clock vs the PHY providing it.
4. **MPU & Cache**: If the MAC counters increment but the DMA descriptors don't update, verify that the MPU region size and attributes for `0x30000000` are perfectly aligned and applied.
5. **Promiscuous Mode**: Ensure `filterConf.PromiscuousMode = ENABLE;` is actually taking effect in the MAC filtering registers.
