/**
 * @file
 * @author Ádám Kiss
 * @author Zsombor Bodnár
 */

#ifndef MUEB4_FIRMWARE_CORE_INC_NETWORK_H_
#define MUEB4_FIRMWARE_CORE_INC_NETWORK_H_

#include <array>
#include <cstdint>
#include <tuple>

#include "art_net.h"
#include "panel.h"
#include "wizchip_conf.h"

/**
 * Manages all network related functionality.
 */
class Network final {
 public:
  Network(const Network &) = delete;
  Network &operator=(const Network &) = delete;

  /// Returns a Network instance.
  static Network &Instance();

  /// Network class' loop.
  void Step();

  void UpdateIp();

  void IpAssign();

  void IpUpdate();

  void SyncTimedOut();

  void IpConflict() const;

  void StreamTerminated();

 private:
  /// Command socket port number.
  static constexpr std::uint16_t kCommandSocketPort{50000u};

  /// Bootloader firmware updater port number.
  static constexpr std::uint16_t kFirmwareUpdaterPort{50002u};

  /// EUI-48 MAC start address.
  static constexpr std::uint16_t kEui48MacStartAddress{0xFAu};

  /// EEPROM I2C device address.
  static constexpr std::uint8_t kEepromAddress{0b10100001u};

  /// Firmware update magic number
  static constexpr std::uint32_t kFirmwareUpdateMagic{0xA53C91E7u};

  /// DHCP socket number.
  static constexpr std::uint8_t kDhcpSocket{0u};

  /// Command socket number.
  static constexpr std::uint8_t kCommandSocket{1u};

  /// Animation socket number.
  static constexpr std::uint8_t kE131Socket{2u};

  static constexpr std::uint8_t kE131SyncSocket{3u};

  static constexpr std::uint8_t kArtNetSocket{4u};

  /// Bootloader firmware updater Socket number.
  static constexpr std::uint8_t kFirmwareUpdaterSocket{5u};

  Network();
  ~Network() = default;

  void OpenMulticastSocket(const std::uint8_t &socket_number,
                           const std::uint8_t &third_octet,
                           const std::uint8_t &last_octet) const;

  auto CheckIpAddress(const std::uint8_t &socket_number);

  void SetPanelColorData(const std::array<std::uint8_t, 512u> &dmx_data) const;

  /// Handles animation protocol.
  void HandleE131Packet(const std::uint8_t &socket_number);

  void HandleArtNetPacket(const std::uint8_t &socket_number);

  /**
   * Handles remote command.
   * @see #Command.
   */
  void HandleCommandProtocol();

  void BootloaderFirmwareUpdater();

  void SendFirmwareCrc(const std::uint32_t pBuffer[], const std::uint32_t &size,
                       const std::array<std::uint8_t, 4> &server_address,
                       const std::uint16_t &server_port);

  ArtPollReply art_poll_reply_;

  Panel &left_panel{Panel::left_panel()};

  Panel &right_panel{Panel::right_panel()};

  /**
   * DHCP RX buffer.
   */
  std::array<std::uint8_t, 576u> dhcp_rx_buffer_{0u};

  /**
   * Stores network information.
   * Contains MAC address, Source IP, Subnet mask etc.
   */
  wiz_NetInfo wiz_net_info_{};

  std::array<std::uint8_t, 16u> cid_{0u};

  std::array<std::uint8_t, 4u> last_ip_address_{0u};

  const std::array<std::uint8_t, 4u> broadcast_ip_address_{10u, 6u, 255u, 255u};

  std::uint16_t dmx_buffer_offset_{0u};

  std::uint16_t synchronization_address_{0u};

  std::uint8_t data_sequence_number_{0u};

  std::uint8_t sync_sequence_number_{0u};

  std::uint8_t priority_{0u};

  std::uint8_t universe_number_{0u};

  bool synced_{false};

  bool force_synchronization_{false};

  bool art_net_data_mode{false};
};

#endif  // MUEB4_FIRMWARE_CORE_INC_NETWORK_H_
