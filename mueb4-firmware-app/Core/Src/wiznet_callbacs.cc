#include "wiznet_callbacs.h"

#include <cstdint>

#include "main.h"
#include "spi.h"

void CrisEn() { __disable_irq(); }

void CrisEx() { __enable_irq(); }

/**
 * Full-duplex transfer of one byte, register level instead of HAL.
 * The HAL call overhead is more than the transfer time of the short W5500
 * accesses. Every byte is waited for, the RX FIFO stays empty.
 */
static std::uint8_t SpiTransfer(std::uint8_t data) {
  SPI_TypeDef *const spi{hspi1.Instance};
  // 8 bit access, a 16 bit write would send 2 bytes
  auto *const dr{reinterpret_cast<volatile std::uint8_t *>(&spi->DR)};

  // Enabled by the first transfer like HAL does
  if (!(spi->CR1 & SPI_CR1_SPE)) {
    spi->CR1 |= SPI_CR1_SPE;
  }

  *dr = data;
  while (!(spi->SR & SPI_SR_RXNE)) {
  }

  return *dr;
}

void CsSel() { SPI1_NSS_GPIO_Port->BRR = SPI1_NSS_Pin; }

void CsDesel() { SPI1_NSS_GPIO_Port->BSRR = SPI1_NSS_Pin; }

std::uint8_t SpiRb() { return SpiTransfer(0xFFu); }

void SpiWb(std::uint8_t pData) { SpiTransfer(pData); }

void SpiRBurst(std::uint8_t *pData, std::uint16_t Size) {
  for (std::uint16_t i{0u}; i < Size; i++) {
    pData[i] = SpiTransfer(0xFFu);
  }
}

void SpiWBurst(std::uint8_t *pData, std::uint16_t Size) {
  for (std::uint16_t i{0u}; i < Size; i++) {
    SpiTransfer(pData[i]);
  }
}
