/**
 * @file
 * @author Ádám Kiss
 * @author Zsombor Bodnár
 */

#ifndef MUEB4_FIRMWARE_CORE_INC_NETWORK_H_
#define MUEB4_FIRMWARE_CORE_INC_NETWORK_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

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

  /// Called from interrupt, Step() terminates the stream.
  void StreamTimedOut();

  /// Called from interrupt, Step() clears the synchronization.
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

  /**
   * Art-Net packets handled in one Step().
   * Every node receives all broadcast universes, the own packet must not
   * wait behind the others. Limited so the other sockets aren't starved.
   */
  static constexpr std::uint8_t kMaxArtNetPacketsPerStep{8u};

  /// The node's DMX data size, left and right panel.
  static constexpr std::size_t kPanelDataSize{2u * Panel::kColorDataSize};

  Network();
  ~Network() = default;

  void OpenMulticastSocket(const std::uint8_t &socket_number,
                           const std::uint8_t &third_octet,
                           const std::uint8_t &last_octet) const;

  /// UDP packet in a socket's W5500 RX buffer.
  struct UdpPacket {
    std::array<std::uint8_t, 4u> server_address;
    std::uint16_t server_port;
    /// Payload size
    std::uint16_t size;
    /// W5500 RX buffer pointer of the payload
    std::uint16_t data_pointer;
  };

  /// Handles a packet still in the W5500 RX buffer.
  using PacketHandler = void (Network::*)(const std::uint8_t &socket_number,
                                          const UdpPacket &packet);

  /**
   * Receives a UDP packet, replaces recvfrom() on the UDP sockets.
   * Calls handler for packets from the server network, then frees the packet.
   * The handler reads only the bytes it needs with #ReadUdpPayload.
   * @warning recvfrom() must not be used on these sockets, it would return
   * the unread rest of a packet in the next call
   */
  void ReceiveUdpPacket(const std::uint8_t &socket_number,
                        PacketHandler handler);

  /**
   * Reads from the packet's payload at offset.
   * Reads at most until the end of the payload, the rest of buffer is kept.
   */
  static void ReadUdpPayload(const std::uint8_t &socket_number,
                             const UdpPacket &packet,
                             const std::uint16_t &offset,
                             std::span<std::uint8_t> buffer);

  /// Sets both panels from the node's part of the DMX data.
  void SetPanelColorData(
      std::span<const std::uint8_t, kPanelDataSize> panel_data) const;

  /// Handles animation protocol.
  void HandleE131Packet(const std::uint8_t &socket_number,
                        const UdpPacket &packet);

  /**
   * Handles an Art-Net packet.
   * Every node gets all broadcast universes, only the headers and the node's
   * data are read.
   */
  void HandleArtNetPacket(const std::uint8_t &socket_number,
                          const UdpPacket &packet);

  /**
   * Handles remote command.
   * @see #Command.
   */
  void HandleCommandProtocol(const std::uint8_t &socket_number,
                             const UdpPacket &packet);

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

  std::uint16_t dmx_buffer_offset_{0u};

  std::uint16_t synchronization_address_{0u};

  std::uint8_t data_sequence_number_{0u};

  std::uint8_t sync_sequence_number_{0u};

  std::uint8_t priority_{0u};

  std::uint8_t universe_number_{0u};

  bool synced_{false};

  /// E1.31 sync packets stopped after the node was synchronized.
  bool sync_lost_{false};

  bool force_synchronization_{false};

  bool art_net_data_mode{false};

  bool bootloader_firmware_update_requested_{false};

  /// Set by stream timeout timer interrupt.
  volatile bool stream_timed_out_{false};

  /// Set by synchronization timeout timer interrupts.
  volatile bool sync_timed_out_{false};
};

#endif  // MUEB4_FIRMWARE_CORE_INC_NETWORK_H_
