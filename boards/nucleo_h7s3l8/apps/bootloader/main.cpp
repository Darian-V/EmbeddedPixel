#include "board_init.h"
#include "stm32h7rsxx_hal.h"
#include "extmem_manager.h"
#include "console.h"
#include "Crc32.h"
#include "proto/ProtocolTypes.h"
#include <stdio.h>
#include <string.h>

using namespace net::proto;

// Define the application's base address in external flash
#define APPLICATION_ADDRESS     0x70000000
#define RAM_CONTROL_BLOCK_BASE  0x24070000

XSPI_HandleTypeDef hxspi2;
XSPI_HandleTypeDef hxspi1; // Added for EXTMEM macro dependencies

static void MX_XSPI2_Init(void)
{
  XSPIM_CfgTypeDef sXspiManagerCfg = {0};

  /* XSPI2 parameter configuration*/
  hxspi2.Instance = XSPI2;
  hxspi2.Init.FifoThresholdByte = 4;
  hxspi2.Init.MemoryMode = HAL_XSPI_SINGLE_MEM;
  hxspi2.Init.MemoryType = HAL_XSPI_MEMTYPE_MACRONIX;
  hxspi2.Init.MemorySize = HAL_XSPI_SIZE_32GB;
  hxspi2.Init.ChipSelectHighTimeCycle = 2;
  hxspi2.Init.FreeRunningClock = HAL_XSPI_FREERUNCLK_DISABLE;
  hxspi2.Init.ClockMode = HAL_XSPI_CLOCK_MODE_0;
  hxspi2.Init.WrapSize = HAL_XSPI_WRAP_NOT_SUPPORTED;
  hxspi2.Init.ClockPrescaler = 3;
  hxspi2.Init.SampleShifting = HAL_XSPI_SAMPLE_SHIFT_NONE;
  hxspi2.Init.ChipSelectBoundary = HAL_XSPI_BONDARYOF_NONE;
  hxspi2.Init.MaxTran = 0;
  hxspi2.Init.Refresh = 0;
  hxspi2.Init.MemorySelect = HAL_XSPI_CSSEL_NCS1;
  HAL_XSPI_Init(&hxspi2);

  sXspiManagerCfg.nCSOverride = HAL_XSPI_CSSEL_OVR_NCS1;
  sXspiManagerCfg.IOPort = HAL_XSPIM_IOPORT_2;
  sXspiManagerCfg.Req2AckTime = 1U;
  HAL_XSPIM_Config(&hxspi2, &sXspiManagerCfg, HAL_XSPI_TIMEOUT_DEFAULT_VALUE);
}

// Dummy trace for EXTMEM
void EXTMEM_TRACE(uint8_t *Message) {
    (void)Message;
}

static void check_and_install_ota() {
    net::proto::OtaControlBlock blk = {0};
    bool found_valid_block = false;

    // Invalidate D-Cache at control block address before reading
    SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t*>(RAM_CONTROL_BLOCK_BASE), sizeof(net::proto::OtaControlBlock));

    // 1. Check RAM Control Block in top of AXI SRAM
    const auto* ram_blk = reinterpret_cast<const net::proto::OtaControlBlock*>(RAM_CONTROL_BLOCK_BASE);
    printf("OTA: Checking control block at 0x%08lX (magic=0x%08lX, state=%lu)...\r\n",
           static_cast<uint32_t>(RAM_CONTROL_BLOCK_BASE), ram_blk->magic, ram_blk->state);

    if (ram_blk->magic == net::proto::OTA_MAGIC) {
        uint32_t expected_crc = sys::Crc32::Calculate(ram_blk, sizeof(net::proto::OtaControlBlock) - sizeof(ram_blk->struct_crc32));
        if (ram_blk->struct_crc32 == expected_crc &&
            ram_blk->state == static_cast<uint32_t>(net::proto::OtaState::PENDING_INSTALL)) {
            blk = *ram_blk;
            found_valid_block = true;
        } else {
            printf("OTA: Struct CRC check: got=0x%08lX, expected=0x%08lX\r\n",
                   ram_blk->struct_crc32, expected_crc);
        }
    }

    if (!found_valid_block) {
        return;
    }

    if (blk.image_size == 0 || blk.image_size > 8 * 1024 * 1024) {
        printf("OTA: Invalid staged size: %lu bytes\r\n", blk.image_size);
        return;
    }

    printf("\r\n========================================\r\n");
    printf("OTA: Pending update detected!\r\n");
    printf("     Image Size:     %lu bytes\r\n", blk.image_size);
    printf("     Target Version: 0x%08lX\r\n", blk.target_version);
    printf("     Expected CRC32: 0x%08lX\r\n", blk.image_crc32);
    printf("     Staging Source: 0x%08lX\r\n", blk.staging_address);
    printf("========================================\r\n");

    bool is_ram_staging = (blk.staging_address >= 0x20000000 && blk.staging_address < 0x30000000);

    // Invalidate D-Cache for staging buffer before reading
    if (is_ram_staging) {
        SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t*>(blk.staging_address), blk.image_size);
    }

    // 1. Verify Staging CRC32
    printf("OTA: Verifying staging integrity in RAM...\r\n");
    uint32_t staged_crc32 = 0;
    if (is_ram_staging) {
        const uint8_t* ram_src = reinterpret_cast<const uint8_t*>(blk.staging_address);
        staged_crc32 = sys::Crc32::Calculate(ram_src, blk.image_size);
    }

    if (staged_crc32 != blk.image_crc32) {
        printf("OTA: Staged image CRC mismatch! (0x%08lX vs 0x%08lX)\r\n",
               staged_crc32, blk.image_crc32);
        memset(reinterpret_cast<void*>(RAM_CONTROL_BLOCK_BASE), 0, sizeof(blk));
        SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t*>(RAM_CONTROL_BLOCK_BASE), sizeof(blk));
        return;
    }

    // 2. Erase Slot A (0x00000000 in External Flash)
    printf("OTA: Erasing Slot A...\r\n");
    uint32_t erase_size = (blk.image_size + 4095) & ~4095;
    uint32_t cur = 0;
    while (cur < erase_size) {
        if (EXTMEM_EraseSector(0, cur, 4096) != EXTMEM_OK) {
            printf("OTA: Slot A erase failed at offset 0x%08lX!\r\n", cur);
            return;
        }
        cur += 4096;
    }

    // 3. Program firmware from RAM into Slot A
    printf("OTA: Installing firmware to Slot A...\r\n");
    uint8_t buffer[1024];
    uint32_t offset = 0;
    bool write_ok = true;

    while (offset < blk.image_size) {
        uint32_t to_copy = (blk.image_size - offset > sizeof(buffer))
                           ? sizeof(buffer)
                           : (blk.image_size - offset);

        const uint8_t* src_ptr = reinterpret_cast<const uint8_t*>(blk.staging_address + offset);

        if (EXTMEM_Write(0, offset, src_ptr, to_copy) != EXTMEM_OK) {
            write_ok = false;
            break;
        }
        offset += to_copy;
    }

    if (!write_ok) {
        printf("OTA: Firmware write to Slot A failed!\r\n");
        return;
    }

    // 4. Verify Slot A CRC32
    printf("OTA: Validating Slot A installation...\r\n");
    uint32_t current_crc = sys::Crc32::INITIAL_REMAINDER;
    offset = 0;
    bool read_ok = true;
    while (offset < blk.image_size) {
        uint32_t to_read = (blk.image_size - offset > sizeof(buffer))
                           ? sizeof(buffer)
                           : (blk.image_size - offset);
        if (EXTMEM_Read(0, offset, buffer, to_read) != EXTMEM_OK) {
            read_ok = false;
            break;
        }
        current_crc = sys::Crc32::Update(current_crc, buffer, to_read);
        offset += to_read;
    }

    uint32_t slot_a_crc32 = sys::Crc32::Finalize(current_crc);
    if (!read_ok || slot_a_crc32 != blk.image_crc32) {
        printf("OTA: Slot A verification FAILED (0x%08lX vs 0x%08lX)!\r\n",
               slot_a_crc32, blk.image_crc32);
        return;
    }

    // 5. Clear RAM Control Block
    memset(reinterpret_cast<void*>(RAM_CONTROL_BLOCK_BASE), 0, sizeof(blk));
    SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t*>(RAM_CONTROL_BLOCK_BASE), sizeof(blk));

    printf("OTA: Installation SUCCESSFUL! Updated to version 0x%08lX.\r\n\r\n", blk.target_version);
}

typedef void (*pFunction)(void);

static void JumpToApplication(uint32_t appAddress)
{
    uint32_t JumpAddress;
    pFunction JumpToApp;

    JumpAddress = *(__IO uint32_t*) (appAddress + 4);
    JumpToApp = (pFunction) JumpAddress;

    printf("Jumping to Application...\r\n");
    printf("Application MSP: 0x%08lX\r\n", *(__IO uint32_t*) appAddress);
    printf("Application Reset Handler: 0x%08lX\r\n\r\n", JumpAddress);

    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;

    __set_MSP(*(__IO uint32_t*) appAddress);
    JumpToApp();
}

static const char* const BOOTLOADER_BANNER_LINES[] = {
    R"(   ('-.  _   .-')   .-. .-')    ('-.  _ .-') _  _ .-') _     ('-.  _ .-') _        )",
    R"( _(  OO)( '.( OO )_ \  ( OO ) _(  OO)( (  OO) )( (  OO) )  _(  OO)( (  OO) )       )",
    R"((,------.,--.   ,--.);-----.\(,------.\     .'_ \     .'_ (,------.\     .'_       )",
    R"( |  .---'|   `.'   | | .-.  | |  .---',`'--..._),`'--..._) |  .---',`'--..._)      )",
    R"( |  |    |         | | '-' /_)|  |    |  |  \  '|  |  \  ' |  |    |  |  \  '      )",
    R"((|  '--. |  |'.'|  | | .-. `.(|  '--. |  |   ' ||  |   ' |(|  '--. |  |   ' |      )",
    R"( |  .--' |  |   |  | | |  \  ||  .--' |  |   / :|  |   / : |  .--' |  |   / :      )",
    R"( |  `---.|  |   |  | | '--'  /|  `---.|  '--'  /|  '--'  / |  `---.|  '--'  /      )",
    R"( `------'`--'   `--' `------' `------'`-------' `-------'  `------'`-------'       )",
    R"(   _ (`-.        ) (`-.        ('-.                                                )",
    R"(  ( (OO  )        ( OO ).    _(  OO)                                               )",
    R"( _.`     \ ,-.-')(_/.  \_)-.(,------.,--.                                          )",
    R"((__...--'' |  |OO)\  `.'  /  |  .---'|  |.-')                                      )",
    R"( |  /  | | |  |  \ \     /\  |  |    |  | OO )                                     )",
    R"( |  |_.' | |  |(_/  \   \ | (|  '--. |  |`-' |                                     )",
    R"( |  .___.',|  |_.' .'    \_) |  .--'(|  '---.'                                     )",
    R"( |  |    (_|  |   /  .'.  \  |  `---.|      |                                      )",
    R"( `--'      `--'  '--'   '--' `------'`------'                                      )"
};

int main(void)
{
    Board_Init();
    console_init(Board_GetDebugUart());
    HAL_Delay(100);

    printf("\r\n=== Bootloader Started ===\r\n\r\n");
    for (const auto* line : BOOTLOADER_BANNER_LINES) {
        printf("%s\r\n", line);
    }
    printf("\r\n");

    MX_XSPI2_Init();
    printf("XSPI2 Initialized.\r\n");

    int32_t status = MX_EXTMEM_MANAGER_Init();
    printf("EXTMEM_Init status: %ld\r\n", status);

    if (status == 0) {
        // Check and apply any pending OTA update
        check_and_install_ota();

        EXTMEM_MemoryMappedMode(0, EXTMEM_ENABLE);
        printf("External Flash mapped to 0x70000000.\r\n");
        JumpToApplication(APPLICATION_ADDRESS);
    } else {
        printf("EXTMEM Init Failed! Entering recovery loop.\r\n");
        hal::IGpio& redLed = Board_GetRedLed();
        while (1) {
            redLed.toggle();
            for (volatile int i = 0; i < 5000000; i++);
        }
    }

    while (1) {}
}
