/**
 * This file contains bootloader firmware updater functionality.
 * @file
 * @author Zsombor Bodnár
 */

#include <socket.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "main.h"
#include "network.h"

extern std::uint32_t _bootloader_pages[];

namespace {
constexpr std::uint32_t kBaseAddress{FLASH_BASE};
const std::uint32_t kPages{reinterpret_cast<std::uint32_t>(_bootloader_pages)};
}  // namespace

extern "C" {
volatile std::uint8_t bootloader_firmware_update_timeout{0u};
}

/**
 * Manages bootloader firmware update process.
 * @Note At this point the device should have an IP address, and all peripherals
 * should be initialized.
 */
void Network::BootloaderFirmwareUpdater() {
  // For program and erase operations on the Flash memory (write/erase), the
  // internal RC oscillator (HSI) must be ON.
  if (RCC_OscInitTypeDef RCC_OscInitStruct = {.OscillatorType =
                                                  RCC_OSCILLATORTYPE_HSI,
                                              .HSIState = RCC_HSI_ON};
      HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    HAL_NVIC_SystemReset();
  }

restart_update:
  if (socket(kFirmwareUpdaterSocket, Sn_MR_TCP,
             kFirmwareUpdaterPort,
             0x00u) != kFirmwareUpdaterSocket) {
    goto restart_update;
  }

  // Enable TCP keep alive
  setSn_KPALVTR(kFirmwareUpdaterSocket, 1u);

  if (listen(kFirmwareUpdaterSocket) != SOCK_OK) {
    goto restart_update;
  }

  std::uint8_t status;

  bootloader_firmware_update_timeout = 0u;  // Start timeout measurement
  while (true) {
    if (bootloader_firmware_update_timeout == 10u) {
      return;
    }

    getsockopt(kFirmwareUpdaterSocket, SO_STATUS, &status);
    if (status == SOCK_ESTABLISHED) {
      break;
    } else if (status == SOCK_CLOSED) {
      close(kFirmwareUpdaterSocket);
      goto restart_update;
    }
  }

  /* The function HAL_FLASH_Unlock() should be called before to unlock the FLASH
   * interface
   */
  if (HAL_FLASH_Unlock() != HAL_OK) {
    close(kFirmwareUpdaterSocket);
    goto restart_update;
  }

  // FLASH should be previously erased before new programming
  FLASH_EraseInitTypeDef pEraseInit{FLASH_TYPEERASE_PAGES, kBaseAddress,
                                    kPages};
  if (std::uint32_t PageError;
      HAL_FLASHEx_Erase(&pEraseInit, &PageError) != HAL_OK) {
    close(kFirmwareUpdaterSocket);
    goto restart_update;
  }

  // Write flash page by page
  std::int32_t received_size;
  std::uint32_t base_address{kBaseAddress};
  do {
    // Send dummy packet to generate keep alive
    send(kFirmwareUpdaterSocket, (std::uint8_t *)"!", 2u);

    std::array<std::uint8_t, FLASH_PAGE_SIZE> flash_page_buffer{};
    const std::uint32_t *flash_page_buffer_p{
        reinterpret_cast<std::uint32_t *>(flash_page_buffer.data())};

    received_size =
        recv(kFirmwareUpdaterSocket, flash_page_buffer.data(), FLASH_PAGE_SIZE);
    if (received_size < 0) {
      goto restart_update;
    }

    // Overwrite protection
    if (base_address + received_size >= kBaseAddress + kPages * 1024u) {
      // At this point no return to main program, need to restart update
      close(kFirmwareUpdaterSocket);
      goto restart_update;
    }

    std::size_t iterations = received_size / 4u;
    for (std::size_t i{0u}; i < iterations || (iterations == 0 && i < 1); i++) {
      if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, base_address + i * 4u,
                            flash_page_buffer_p[i]) != HAL_OK) {
        close(kFirmwareUpdaterSocket);
        goto restart_update;
      }
    }

    base_address += received_size;
    getsockopt(kFirmwareUpdaterSocket, SO_STATUS, &status);
  } while (getSn_RX_RSR(kFirmwareUpdaterSocket) != 0u ||
           status != SOCK_CLOSE_WAIT);

  send(kFirmwareUpdaterSocket, (std::uint8_t *)"FLASH_OK", 9u);
  disconnect(kFirmwareUpdaterSocket);
  close(kFirmwareUpdaterSocket);

  /* The function HAL_FLASH_Lock() should be called after to lock the FLASH
   * interface
   */
  HAL_FLASH_Lock();
}
