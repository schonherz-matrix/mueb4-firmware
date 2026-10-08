/**
 * This file contains bootloader firmware updater functionality.
 * @file
 * @author Zsombor Bodnár
 */

#include <socket.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "crc.h"
#include "main.h"
#include "network.h"

extern std::uint32_t _bootloader_pages[];

namespace {
constexpr std::uint32_t kBaseAddress{FLASH_BASE};
const std::uint32_t kPages{reinterpret_cast<std::uint32_t>(_bootloader_pages)};

/// Image header: size and CRC-32 (same as zlib.crc32), big endian
constexpr std::size_t kImageHeaderSize{8u};

std::uint32_t ReadBigEndian(const std::uint8_t *data) {
  return std::uint32_t{data[0]} << 24u | std::uint32_t{data[1]} << 16u |
         std::uint32_t{data[2]} << 8u | std::uint32_t{data[3]};
}
}  // namespace

volatile std::uint8_t bootloader_firmware_update_timeout{0u};

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

  // After erase there is no return to main program, the bootloader is gone
  bool flash_erased{false};

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
    // Wait for the flasher to reconnect as long as needed after erase
    if (bootloader_firmware_update_timeout == 10u && !flash_erased) {
      close(kFirmwareUpdaterSocket);
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

  // Validate the image header before erasing anything
  while (getSn_RX_RSR(kFirmwareUpdaterSocket) < kImageHeaderSize) {
    if (bootloader_firmware_update_timeout == 10u && !flash_erased) {
      close(kFirmwareUpdaterSocket);
      return;
    }

    getsockopt(kFirmwareUpdaterSocket, SO_STATUS, &status);
    if (status != SOCK_ESTABLISHED) {
      close(kFirmwareUpdaterSocket);
      goto restart_update;
    }
  }

  std::array<std::uint8_t, kImageHeaderSize> header;
  recv(kFirmwareUpdaterSocket, header.data(), kImageHeaderSize);
  const std::uint32_t image_size{ReadBigEndian(header.data())};
  const std::uint32_t image_crc{ReadBigEndian(header.data() + 4u)};
  if (image_size == 0u || image_size % 4u != 0u ||
      image_size > kPages * FLASH_PAGE_SIZE) {
    send(kFirmwareUpdaterSocket, (std::uint8_t *)"FLASH_ERROR", 12u);
    disconnect(kFirmwareUpdaterSocket);
    close(kFirmwareUpdaterSocket);
    goto restart_update;
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
  flash_erased = true;
  if (std::uint32_t PageError;
      HAL_FLASHEx_Erase(&pEraseInit, &PageError) != HAL_OK) {
    close(kFirmwareUpdaterSocket);
    goto restart_update;
  }

  // Write flash page by page, TCP may split the stream at any byte
  alignas(std::uint32_t) std::array<std::uint8_t, FLASH_PAGE_SIZE>
      flash_page_buffer;
  const std::uint32_t *flash_page_buffer_p{
      reinterpret_cast<std::uint32_t *>(flash_page_buffer.data())};
  std::size_t buffered_size{0u};
  std::uint32_t base_address{kBaseAddress};
  bool stream_ended;
  do {
    // Status first, data received before FIN is already in RX buffer
    getsockopt(kFirmwareUpdaterSocket, SO_STATUS, &status);
    const std::uint16_t available_size{getSn_RX_RSR(kFirmwareUpdaterSocket)};
    stream_ended = available_size == 0u && status == SOCK_CLOSE_WAIT;

    if (available_size == 0u && !stream_ended) {
      if (status != SOCK_ESTABLISHED) {
        close(kFirmwareUpdaterSocket);
        goto restart_update;
      }

      // Wait for data, recv() would close the socket on FIN
      continue;
    }

    if (available_size != 0u) {
      // Send dummy packet to generate keep alive
      send(kFirmwareUpdaterSocket, (std::uint8_t *)"!", 2u);

      // Erased flash value, pads the last word of the image
      if (buffered_size == 0u) {
        flash_page_buffer.fill(0xFFu);
      }

      const std::int32_t received_size{recv(
          kFirmwareUpdaterSocket, flash_page_buffer.data() + buffered_size,
          static_cast<std::uint16_t>(
              std::min<std::size_t>(available_size,
                                    FLASH_PAGE_SIZE - buffered_size)))};
      if (received_size < 0) {
        goto restart_update;
      }
      buffered_size += received_size;
    }

    if (buffered_size == FLASH_PAGE_SIZE || (stream_ended && buffered_size)) {
      // Round up to whole words
      const std::size_t write_size{(buffered_size + 3u) & ~std::size_t{3u}};

      // Overwrite protection, image_size is checked against the flash size
      if (base_address + write_size > kBaseAddress + image_size) {
        // At this point no return to main program, need to restart update
        close(kFirmwareUpdaterSocket);
        goto restart_update;
      }

      for (std::size_t i{0u}; i < write_size / 4u; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, base_address + i * 4u,
                              flash_page_buffer_p[i]) != HAL_OK) {
          close(kFirmwareUpdaterSocket);
          goto restart_update;
        }
      }

      base_address += write_size;
      buffered_size = 0u;
    }
  } while (!stream_ended);

  // Verify what was written to flash, on error wait for the flasher to retry
  if (base_address != kBaseAddress + image_size ||
      ~HAL_CRC_Calculate(&hcrc, reinterpret_cast<std::uint32_t *>(kBaseAddress),
                         image_size / 4u) != image_crc) {
    send(kFirmwareUpdaterSocket, (std::uint8_t *)"FLASH_ERROR", 12u);
    disconnect(kFirmwareUpdaterSocket);
    close(kFirmwareUpdaterSocket);
    goto restart_update;
  }

  send(kFirmwareUpdaterSocket, (std::uint8_t *)"FLASH_OK", 9u);
  disconnect(kFirmwareUpdaterSocket);
  close(kFirmwareUpdaterSocket);

  /* The function HAL_FLASH_Lock() should be called after to lock the FLASH
   * interface
   */
  HAL_FLASH_Lock();
}
