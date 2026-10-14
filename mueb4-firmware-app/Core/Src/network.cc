/**
 * @file
 * @author Ádám Kiss
 * @author Zsombor Bodnár
 */

#include "network.h"

#include <dhcp.h>
#include <socket.h>

#include <boost/endian/conversion.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <span>

#include "command.h"
#include "crc.h"
#include "e131.h"
#include "i2c.h"
#include "main.h"
#include "tim.h"
#include "version.h"
#include "wiznet_callbacs.h"

extern std::uint32_t _siisr_vector[];
extern std::uint32_t _main_program_size[];
extern std::uint32_t _bootloader_size[];

extern "C" {
volatile std::uint32_t firmware_update_enabler
    __attribute__((section(".firmware_update_enabler")));

static void CIpAssign() { Network::Instance().IpAssign(); }

static void CIpUpdate() { Network::Instance().IpUpdate(); }

static void CIpConflict() { Network::Instance().IpConflict(); }
}

static const char *const status_format{
    // clang-format off
  "MUEB FW: %s\n"
  "MUEB MAC: %x:%x:%x:%x:%x:%x\n"
  "Internal animation: %s\n"
  "Left panel state: %#x\n"
  "Right panel state: %#x\n"
  "SEM & KSZK forever",
    // clang-format on
};

/**
 * E1.31 6.7.2 sequence numbering, also used for Art-Net.
 * The difference is calculated in 8 bit to handle the wrap around.
 * @return True if the packet is out of order and has to be discarded
 */
static bool IsSequenceOutOfOrder(const std::uint8_t &sequence_number,
                                 const std::uint8_t &last_sequence_number) {
  const auto difference{
      static_cast<std::int8_t>(sequence_number - last_sequence_number)};

  return difference <= 0 && difference > -20;
}

/**
 * Reads from the socket's W5500 RX buffer at pointer.
 * Sn_RX_RD isn't changed, the W5500 wraps the pointer in the buffer.
 */
static void ReadRxBuffer(const std::uint8_t &socket_number,
                         const std::uint16_t &pointer,
                         std::span<std::uint8_t> data) {
  WIZCHIP_READ_BUF((static_cast<std::uint32_t>(pointer) << 8u) +
                       (WIZCHIP_RXBUF_BLOCK(socket_number) << 3u),
                   data.data(), static_cast<std::uint16_t>(data.size()));
}

Network &Network::Instance() {
  static Network network;

  return network;
}

void Network::Step() {
  // Timer events are only flagged in interrupts, handled here
  if (stream_timed_out_) {
    stream_timed_out_ = false;
    StreamTerminated();
  }

  if (sync_timed_out_) {
    sync_timed_out_ = false;
    sync_lost_ = synced_;
    synced_ = false;
  }

  if (wizphy_getphylink() == PHY_LINK_ON) {
    HAL_GPIO_WritePin(LED_JOKER_GPIO_Port, LED_JOKER_Pin, GPIO_PIN_SET);

    if (std::uint8_t dhcp_status = DHCP_run(); dhcp_status != DHCP_IP_ASSIGN &&
                                               dhcp_status != DHCP_IP_CHANGED &&
                                               dhcp_status != DHCP_IP_LEASED) {
      HAL_GPIO_WritePin(LED_DHCP_GPIO_Port, LED_DHCP_Pin, GPIO_PIN_RESET);
      return;
    }

    HAL_GPIO_WritePin(LED_DHCP_GPIO_Port, LED_DHCP_Pin, GPIO_PIN_SET);

    if (getSn_RX_RSR(kCommandSocket) > 0u) {
      ReceiveUdpPacket(kCommandSocket, &Network::HandleCommandProtocol);
    }

    // Called outside of HandleCommandProtocol(), the command packet is
    // released before the update starts
    if (bootloader_firmware_update_requested_) {
      bootloader_firmware_update_requested_ = false;
      BootloaderFirmwareUpdater();
    }

    // Data before sync, otherwise a sync packet waiting with the data would
    // output the previous frame
    if (!art_net_data_mode && getSn_RX_RSR(kE131Socket) > 0u) {
      ReceiveUdpPacket(kE131Socket, &Network::HandleE131Packet);
    }

    if (!art_net_data_mode && getSn_RX_RSR(kE131SyncSocket) > 0u) {
      ReceiveUdpPacket(kE131SyncSocket, &Network::HandleE131Packet);
    }

    for (std::uint8_t i{0u};
         i < kMaxArtNetPacketsPerStep && getSn_RX_RSR(kArtNetSocket) > 0u;
         i++) {
      ReceiveUdpPacket(kArtNetSocket, &Network::HandleArtNetPacket);
    }
  } else {
    HAL_GPIO_WritePin(LED_JOKER_GPIO_Port, LED_JOKER_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_DHCP_GPIO_Port, LED_DHCP_Pin, GPIO_PIN_RESET);
    DhcpRebind();
  }
}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "readability-convert-member-functions-to-static"
void Network::OpenMulticastSocket(const std::uint8_t &socket_number,
                                  const std::uint8_t &third_octet,
                                  const std::uint8_t &last_octet) const {
  std::array<std::uint8_t, 4> multicast_address{239u, 255u, third_octet,
                                                last_octet};
  std::array<std::uint8_t, 6> multicast_hardware_address{
      0x01u, 0x00u, 0x5eu, 0x7fu, third_octet, last_octet};

  // Configure multicast, IGMPv2
  setSn_DHAR(socket_number,
             const_cast<std::uint8_t *>(multicast_hardware_address.data()));
  setSn_DIPR(socket_number,
             const_cast<std::uint8_t *>(multicast_address.data()));
  setSn_DPORT(socket_number, ACN_SDT_MULTICAST_PORT);

  socket(socket_number, Sn_MR_UDP, ACN_SDT_MULTICAST_PORT,
         SF_MULTI_ENABLE | SF_BROAD_BLOCK);
}
#pragma clang diagnostic pop

void Network::UpdateIp() {
  wizchip_getnetinfo(&wiz_net_info_);
  HAL_GPIO_WritePin(LED_DHCP_GPIO_Port, LED_DHCP_Pin, GPIO_PIN_SET);

  std::uint8_t level_number = 18u - wiz_net_info_.ip[2];
  std::uint8_t room_number = wiz_net_info_.ip[3] - 5u;

  dmx_buffer_offset_ = (level_number * 192u + room_number * 24u) % 504u;

  universe_number_ = ((level_number * 8u + room_number) / 21u) + 1u;
  OpenMulticastSocket(kE131Socket, 0u, universe_number_);

  art_poll_reply_.ip_address = std::to_array(wiz_net_info_.ip);
  art_poll_reply_.bind_ip = art_poll_reply_.ip_address;

  // Art-Net 4: ArtPollReply mustn't be broadcast, the controller learns the
  // change from the reply to its next ArtPoll
  art_poll_reply_.sw_out[0] = universe_number_;
}

void Network::IpAssign() {
  default_ip_assign();
  UpdateIp();
}

void Network::IpUpdate() {
  default_ip_update();
  UpdateIp();
}

void Network::StreamTimedOut() { stream_timed_out_ = true; }

void Network::SyncTimedOut() { sync_timed_out_ = true; }

#pragma clang diagnostic push
#pragma ide diagnostic ignored "readability-convert-member-functions-to-static"
void Network::IpConflict() const {
  default_ip_conflict();
  HAL_GPIO_WritePin(LED_DHCP_GPIO_Port, LED_DHCP_Pin, GPIO_PIN_RESET);
}
#pragma clang diagnostic pop

void Network::StreamTerminated() {
  HAL_GPIO_WritePin(LED_SERVER_GPIO_Port, LED_SERVER_Pin, GPIO_PIN_RESET);

  cid_.fill(0u);
  last_ip_address_.fill(0u);
  synchronization_address_ = 0;
  data_sequence_number_ = 0;
  sync_sequence_number_ = 0;
  priority_ = 0;
  synced_ = false;
  sync_lost_ = false;
  force_synchronization_ = false;

  HAL_TIM_Base_Stop_IT(&htim16);
  HAL_NVIC_ClearPendingIRQ(TIM16_IRQn);
  htim16.Instance->EGR = TIM_EGR_UG;
  CLEAR_BIT(htim16.Instance->SR, TIM_SR_UIF);
  stream_timed_out_ = false;

  close(kE131SyncSocket);
  HAL_TIM_Base_Stop_IT(&htim17);
  HAL_NVIC_ClearPendingIRQ(TIM17_IRQn);
  htim17.Instance->EGR = TIM_EGR_UG;
  CLEAR_BIT(htim17.Instance->SR, TIM_SR_UIF);
  HAL_TIM_Base_Stop_IT(&htim14);
  HAL_NVIC_ClearPendingIRQ(TIM14_IRQn);
  htim14.Instance->EGR = TIM_EGR_UG;
  CLEAR_BIT(htim14.Instance->SR, TIM_SR_UIF);
  sync_timed_out_ = false;

  left_panel.Blank();
  right_panel.Blank();
}

Network::Network() {
  // -- Do not remove --
  // Hardware reset W5500
  HAL_GPIO_WritePin(W5500_RSTN_GPIO_Port, W5500_RSTN_Pin, GPIO_PIN_RESET);
  // RESET should be held low at least 500 us for W5500
  HAL_Delay(1u);
  HAL_GPIO_WritePin(W5500_RSTN_GPIO_Port, W5500_RSTN_Pin, GPIO_PIN_SET);
  // RSTn to internal PLOCK (PLL Lock)
  HAL_Delay(1u);
  // -- Do not remove --

  // Register W5500 callback functions
  reg_wizchip_cris_cbfunc(CrisEn, CrisEx);
  reg_wizchip_cs_cbfunc(CsSel, CsDesel);
  reg_wizchip_spi_cbfunc(SpiRb, SpiWb);
  reg_wizchip_spiburst_cbfunc(SpiRBurst, SpiWBurst);
  reg_dhcp_cbfunc(CIpAssign, CIpUpdate, CIpConflict);

  // DHCP, command protocol, E1.31 data/sync, E1.31 sync,
  // Art-Net, bootloader firmware rx/tx sizes in KB, max 16 KB each.
  // Art-Net gets every broadcast universe, 8 KB holds about 3 frames of 5
  // universes. E1.31 data and sync sockets get 1 universe, 2 KB holds 3
  // packets.
  std::array<std::uint8_t, 8u> tx_size{1u, 1u, 1u, 1u, 1u, 1u, 0u, 0u};
  std::array<std::uint8_t, 8u> rx_size{1u, 1u, 2u, 2u, 8u, 2u, 0u, 0u};

  // This includes soft reset
  wizchip_init(tx_size.data(), rx_size.data());

  // Gets MAC address from EEPROM.
  // The device uses Microchip 24AA02E48T-I/OT EEPROM
  HAL_I2C_Mem_Read(&hi2c2, kEepromAddress, kEui48MacStartAddress,
                   I2C_MEMADD_SIZE_8BIT, wiz_net_info_.mac, 6u, HAL_MAX_DELAY);
  setSHAR(wiz_net_info_.mac);
  art_poll_reply_.mac = std::to_array(wiz_net_info_.mac);

  // Set all capable, Auto-negotiation enabled
  wiz_PhyConf_t phyconf{PHY_CONFBY_SW, PHY_MODE_AUTONEGO};
  wizphy_setphyconf(&phyconf);

  // DHCP 1s timer located in interrupts.cc
  DHCP_init(kDhcpSocket, dhcp_rx_buffer_.data());

  socket(kCommandSocket, Sn_MR_UDP, kCommandSocketPort, 0x00u);
  socket(kArtNetSocket, Sn_MR_UDP, kArtNetPort, 0x00u);
}

void Network::ReceiveUdpPacket(const std::uint8_t &socket_number,
                               PacketHandler handler) {
  const auto read_pointer{
      static_cast<std::uint16_t>(getSn_RX_RD(socket_number))};

  // Stored by the W5500 before the payload: sender IP address, port, size
  std::array<std::uint8_t, 8u> info;
  ReadRxBuffer(socket_number, read_pointer, info);
  const UdpPacket packet{
      {info[0], info[1], info[2], info[3]},
      boost::endian::load_big_u16(info.data() + 4u),
      boost::endian::load_big_u16(info.data() + 6u),
      static_cast<std::uint16_t>(read_pointer + info.size())};

  // Servers are in x.x.0.x
  if (packet.server_address[2] == 0u) {
    (this->*handler)(socket_number, packet);
  }

  // Free the whole packet, read or not
  setSn_RX_RD(socket_number,
              static_cast<std::uint16_t>(packet.data_pointer + packet.size));
  setSn_CR(socket_number, Sn_CR_RECV);
  while (getSn_CR(socket_number)) {
  }
}

void Network::ReadUdpPayload(const std::uint8_t &socket_number,
                             const UdpPacket &packet,
                             const std::uint16_t &offset,
                             std::span<std::uint8_t> buffer) {
  if (offset >= packet.size) {
    return;
  }

  ReadRxBuffer(socket_number,
               static_cast<std::uint16_t>(packet.data_pointer + offset),
               buffer.first(std::min<std::size_t>(packet.size - offset,
                                                  buffer.size())));
}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "readability-convert-member-functions-to-static"
void Network::SetPanelColorData(
    std::span<const std::uint8_t, kPanelDataSize> panel_data) const {
  HAL_GPIO_WritePin(LED_SERVER_GPIO_Port, LED_SERVER_Pin, GPIO_PIN_SET);
  Panel::SetInternalAnimation(false);

  Panel::GetPanel(Panel::Side::LEFT)
      .SetColorData(panel_data.first<Panel::kColorDataSize>(), true);
  Panel::GetPanel(Panel::Side::RIGHT)
      .SetColorData(panel_data.last<Panel::kColorDataSize>(), true);
}
#pragma clang diagnostic pop

void Network::HandleE131Packet(const std::uint8_t &socket_number,
                               const UdpPacket &packet) {
  // Largest E1.31 packet, zeroed for shorter ones
  std::array<std::uint8_t, sizeof(E131DataPacket)> buffer{};
  ReadUdpPayload(socket_number, packet, 0u, buffer);

  const auto root_layer{reinterpret_cast<const RootLayer *>(buffer.data())};
  if (root_layer->preamble_size != 0x0010u ||
      root_layer->post_amble_size != 0x0000u ||
      root_layer->acn_packet_identifier != kAcnPacketIdentifier) {
    return;
  }

  if (root_layer->vector == VECTOR_ROOT_E131_DATA) {
    const auto e131DataPacket{
        reinterpret_cast<const E131DataPacket *>(buffer.data())};

    const bool preview_data = e131DataPacket->framing_layer.options & 0x80u;
    const bool stream_terminated =
        e131DataPacket->framing_layer.options & 0x40u;
    const bool force_synchronization =
        e131DataPacket->framing_layer.options & 0x20u;

    // E1.31 6.7.1: data loss is specific to a source and universe, only the
    // current source can terminate the node's universe
    if (stream_terminated) {
      if (e131DataPacket->framing_layer.vector == VECTOR_E131_DATA_PACKET &&
          !preview_data &&
          e131DataPacket->framing_layer.universe == universe_number_ &&
          (cid_ == decltype(cid_){} ||
           e131DataPacket->root_layer.cid == cid_)) {
        StreamTerminated();
      }

      return;
    }

    if (e131DataPacket->framing_layer.vector != VECTOR_E131_DATA_PACKET ||
        (data_sequence_number_ > 0u &&
         IsSequenceOutOfOrder(e131DataPacket->framing_layer.sequence_number,
                              data_sequence_number_)) ||
        // The sync socket also receives the sync universe's data packets
        e131DataPacket->framing_layer.universe != universe_number_ ||
        e131DataPacket->framing_layer.universe == 0u ||
        e131DataPacket->framing_layer.universe > 63999u ||
        e131DataPacket->framing_layer.priority > 200u ||
        e131DataPacket->framing_layer.priority < priority_ ||
        (data_sequence_number_ > 0u && e131DataPacket->root_layer.cid != cid_ &&
         e131DataPacket->framing_layer.priority == priority_) ||
        preview_data ||
        e131DataPacket->dmp_layer.vector != VECTOR_DMP_SET_PROPERTY ||
        e131DataPacket->dmp_layer.address_type_and_data_type != 0xa1u ||
        e131DataPacket->dmp_layer.first_property_address != 0x0000u ||
        e131DataPacket->dmp_layer.address_increment != 0x0001u ||
        e131DataPacket->dmp_layer.property_value_count > 513u ||
        e131DataPacket->dmp_layer.property_value_count < 384u ||
        // Only null START Code data is color data
        e131DataPacket->dmp_layer.start_code != 0u) {
      return;
    }
    cid_ = e131DataPacket->root_layer.cid;
    priority_ = e131DataPacket->framing_layer.priority;
    force_synchronization_ = force_synchronization;
    data_sequence_number_ = e131DataPacket->framing_layer.sequence_number;

    if (const std::uint16_t synchronization_address{
            e131DataPacket->framing_layer.synchronization_address};
        synchronization_address != synchronization_address_) {
      // E1.31 6.2.4.1: a new address is synchronized only after its first
      // sync packet
      close(kE131SyncSocket);
      synced_ = false;
      sync_lost_ = false;
      sync_sequence_number_ = 0u;
      synchronization_address_ = synchronization_address;

      const auto low_byte{
          static_cast<std::uint8_t>(synchronization_address & 0x00FFu)};
      const auto high_byte{
          static_cast<std::uint8_t>((synchronization_address & 0xFF00u) >> 8u)};
      // The node's own universe arrives on the data socket
      if (synchronization_address != 0u &&
          (high_byte != 0u || low_byte != universe_number_)) {
        OpenMulticastSocket(kE131SyncSocket, high_byte, low_byte);
      }
    }

    // Data is kept for the next sync packet, output immediately when
    // unsynchronized. E1.31 6.2.6: after the sync is lost, Force_Synchronization
    // decides whether to output or wait for the sync to resume.
    SetPanelColorData(std::span(e131DataPacket->dmp_layer.property_values)
                          .subspan(dmx_buffer_offset_)
                          .first<kPanelDataSize>());

    if (synchronization_address_ == 0u ||
        (!synced_ && (!sync_lost_ || force_synchronization_))) {
      left_panel.SendColorData();
      right_panel.SendColorData();
    }

    HAL_TIM_Base_Stop_IT(&htim16);
    HAL_NVIC_ClearPendingIRQ(TIM16_IRQn);
    htim16.Instance->EGR = TIM_EGR_UG;
    CLEAR_BIT(htim16.Instance->SR, TIM_SR_UIF);
    stream_timed_out_ = false;
    HAL_TIM_Base_Start_IT(&htim16);
  } else if (root_layer->vector == VECTOR_ROOT_E131_EXTENDED) {
    const auto e131SyncPacket{
        reinterpret_cast<const E131SyncPacket *>(buffer.data())};

    if (e131SyncPacket->framing_layer.vector !=
            VECTOR_E131_EXTENDED_SYNCHRONIZATION ||
        (sync_sequence_number_ > 0u &&
         IsSequenceOutOfOrder(e131SyncPacket->framing_layer.sequence_number,
                              sync_sequence_number_)) ||
        e131SyncPacket->framing_layer.synchronization_address == 0u ||
        e131SyncPacket->framing_layer.synchronization_address !=
            synchronization_address_) {
      return;
    }
    sync_sequence_number_ = e131SyncPacket->framing_layer.sequence_number;

    left_panel.SendColorData();
    right_panel.SendColorData();
    synced_ = true;
    sync_lost_ = false;

    HAL_TIM_Base_Stop_IT(&htim17);
    HAL_NVIC_ClearPendingIRQ(TIM17_IRQn);
    htim17.Instance->EGR = TIM_EGR_UG;
    CLEAR_BIT(htim17.Instance->SR, TIM_SR_UIF);
    sync_timed_out_ = false;
    HAL_TIM_Base_Start_IT(&htim17);
  }
}

void Network::HandleArtNetPacket(const std::uint8_t &socket_number,
                                 const UdpPacket &packet) {
  if (packet.size < sizeof(ArtNetHeader)) {
    return;
  }

  // Largest header used, zeroed for shorter packets
  std::array<std::uint8_t, sizeof(ArtDmx)> buffer{};
  ReadUdpPayload(socket_number, packet, 0u, buffer);

  const auto header{reinterpret_cast<const ArtNetHeader *>(buffer.data())};
  // Art-Net 4: only versions lower than 14 are ignored
  if (header->art_id_op_code.id != kArtNetId || header->prot_ver < 14u) {
    return;
  }

  switch (header->art_id_op_code.op_code) {
    case kOpPoll: {
      // Art-Net 4: unicast to the controller, a broadcast would reach every
      // node's Art-Net socket
      sendto(kArtNetSocket, (std::uint8_t *)&art_poll_reply_,
             sizeof(ArtPollReply),
             const_cast<std::uint8_t *>(packet.server_address.data()),
             kArtNetPort);
      break;
    }
    case kOpOutput: {
      if (!art_net_data_mode) {
        art_net_data_mode = true;
        StreamTerminated();
      }

      const auto art_dmx{reinterpret_cast<const ArtDmx *>(buffer.data())};
      // Filter other universes before sequence check, they have their own
      // sequence numbers
      if (art_dmx->net != art_poll_reply_.net_switch ||
          art_dmx->sub_uni !=
              (art_poll_reply_.sub_switch << 4u | art_poll_reply_.sw_out[0]) ||
          // The node's data must be within the DMX data, which must be in the
          // packet
          art_dmx->length > 512u ||
          art_dmx->length < dmx_buffer_offset_ + kPanelDataSize ||
          packet.size < sizeof(ArtDmx) + art_dmx->length ||
          (last_ip_address_[0] != 0u &&
           last_ip_address_ != packet.server_address)) {
        return;
      }

      // Sequence 0 disables the check, data_sequence_number_ is 0 at start
      if (art_dmx->sequence != 0u) {
        if (data_sequence_number_ != 0u &&
            IsSequenceOutOfOrder(art_dmx->sequence, data_sequence_number_)) {
          return;
        }

        data_sequence_number_ = art_dmx->sequence;
      }
      last_ip_address_ = packet.server_address;

      std::array<std::uint8_t, kPanelDataSize> panel_data;
      ReadUdpPayload(socket_number, packet,
                     static_cast<std::uint16_t>(sizeof(ArtDmx) +
                                                dmx_buffer_offset_),
                     panel_data);
      SetPanelColorData(panel_data);

      if (!synced_) {
        left_panel.SendColorData();
        right_panel.SendColorData();
      }

      HAL_TIM_Base_Stop_IT(&htim16);
      HAL_NVIC_ClearPendingIRQ(TIM16_IRQn);
      htim16.Instance->EGR = TIM_EGR_UG;
      CLEAR_BIT(htim16.Instance->SR, TIM_SR_UIF);
      stream_timed_out_ = false;
      HAL_TIM_Base_Start_IT(&htim16);
      break;
    }
    case kOpSync: {
      if (!art_net_data_mode) {
        art_net_data_mode = true;
        StreamTerminated();
      }

      if (last_ip_address_ != packet.server_address) {
        return;
      }

      synced_ = true;
      left_panel.SendColorData();
      right_panel.SendColorData();

      HAL_TIM_Base_Stop_IT(&htim14);
      HAL_NVIC_ClearPendingIRQ(TIM14_IRQn);
      htim14.Instance->EGR = TIM_EGR_UG;
      CLEAR_BIT(htim14.Instance->SR, TIM_SR_UIF);
      sync_timed_out_ = false;
      HAL_TIM_Base_Start_IT(&htim14);
      break;
    }
    default:
      break;
  }
}

void Network::HandleCommandProtocol(const std::uint8_t &socket_number,
                                    const UdpPacket &packet) {
  // Largest command, zeroed for shorter ones
  std::array<std::uint8_t, sizeof(WhiteBalance)> buffer{};
  ReadUdpPayload(socket_number, packet, 0u, buffer);
  // Replaced by broadcast when the node has no IP address
  auto server_address{packet.server_address};
  const auto &server_port{packet.server_port};

  const auto command = reinterpret_cast<CommandHeader *>(buffer.data());
  if (command->id != kCommandId) {
    return;
  }

  /* When the 6th byte is set to 1 it means we're sending a broadcast command to
   * only one device
   * Can be used when the device doesn't have an IP address
   */
  if (command->is_broadcast) {
    // Return when the MAC address doesn't match
    if (!std::equal(std::begin(wiz_net_info_.mac), std::end(wiz_net_info_.mac),
                    command->mac.cbegin())) {
      return;
    }

    // If the device's IP address is 0.0.0.0 use broadcast target address
    if (std::ranges::all_of(std::begin(wiz_net_info_.ip),
                            std::end(wiz_net_info_.ip),
                            [](const std::uint8_t &i) { return i == 0u; })) {
      server_address.fill(0xFFu);
    }
  }

  switch (static_cast<Command>(command->code)) {
      // Mutable commands
    case Command::kDisablePanels:
      left_panel.Disable();
      right_panel.Disable();
      break;
    case Command::kSetPanelsWhiteBalance: {
      const auto white_balance =
          reinterpret_cast<WhiteBalance *>(buffer.data());
      left_panel.SendWhiteBalance(white_balance->data);
      right_panel.SendWhiteBalance(white_balance->data);
      break;
    }
    case Command::kSetPanelWhiteBalance: {
      const auto white_balance =
          reinterpret_cast<WhiteBalance *>(buffer.data());
      Panel::GetPanel(static_cast<Panel::Side>(white_balance->side))
          .SendWhiteBalance(white_balance->data);
      break;
    }
    case Command::kUseInternalAnimation:
      Panel::SetInternalAnimation(true);
      break;
    case Command::kUseExternalAnimation:
      Panel::SetInternalAnimation(false);
      break;
    case Command::kSwapPanels:
      Panel::Swap();
      break;
    case Command::kBlankPanels:
      left_panel.Blank();
      right_panel.Blank();
      break;
    case Command::kReset:
      HAL_NVIC_SystemReset();
      break;
    case Command::kStartFirmwareUpdate: {
      firmware_update_enabler = kFirmwareUpdateMagic;

      HAL_NVIC_SystemReset();
      break;
    }
    case Command::kStartBootloaderFirmwareUpdate: {
      bootloader_firmware_update_requested_ = true;
      break;
    }
    case Command::kSetArtNet:
      art_net_data_mode = true;
      StreamTerminated();
      break;
    case Command::kSetE131:
      art_net_data_mode = false;
      StreamTerminated();
      break;
    // Immutable commands
    case Command::kPing:
      sendto(kCommandSocket, (std::uint8_t *)"pong", 5u, server_address.data(),
             server_port);
      break;
    case Command::kGetStatus: {
      const auto status_string_size = std::snprintf(
          nullptr, 0u, status_format, mueb_version, wiz_net_info_.mac[0],
          wiz_net_info_.mac[1], wiz_net_info_.mac[2], wiz_net_info_.mac[3],
          wiz_net_info_.mac[4], wiz_net_info_.mac[5],
          (Panel::internal_animation_enabled() ? "on" : "off"),
          static_cast<std::uint8_t>(Panel::GetPanel(Panel::Side::LEFT).state()),
          static_cast<std::uint8_t>(
              Panel::GetPanel(Panel::Side::RIGHT).state()));
      char status_string[status_string_size + 1];

      std::snprintf(
          status_string, sizeof(status_string), status_format, mueb_version,
          wiz_net_info_.mac[0], wiz_net_info_.mac[1], wiz_net_info_.mac[2],
          wiz_net_info_.mac[3], wiz_net_info_.mac[4], wiz_net_info_.mac[5],
          (Panel::internal_animation_enabled() ? "on" : "off"),
          static_cast<std::uint8_t>(Panel::GetPanel(Panel::Side::LEFT).state()),
          static_cast<std::uint8_t>(
              Panel::GetPanel(Panel::Side::RIGHT).state()));

      sendto(kCommandSocket, reinterpret_cast<std::uint8_t *>(status_string),
             static_cast<std::uint16_t>(status_string_size + 1),
             server_address.data(), server_port);
      break;
    }
    case Command::kGetMac: {
      std::array<char, 18u> mac{};
      sendto(kCommandSocket, reinterpret_cast<std::uint8_t *>(mac.data()),
             static_cast<std::uint16_t>(
                 std::snprintf(mac.data(), mac.size(), "%x:%x:%x:%x:%x:%x",
                               wiz_net_info_.mac[0], wiz_net_info_.mac[1],
                               wiz_net_info_.mac[2], wiz_net_info_.mac[3],
                               wiz_net_info_.mac[4], wiz_net_info_.mac[5]) +
                 1),
             server_address.data(), server_port);
      break;
    }
    case Command::kGetFirmwareChecksum: {
      SendFirmwareCrc(_siisr_vector,
                      reinterpret_cast<std::uint32_t>(_main_program_size),
                      server_address, server_port);
      break;
    }
    case Command::kGetBootloaderFirmwareChecksum: {
      SendFirmwareCrc(reinterpret_cast<std::uint32_t *>(FLASH_BASE),
                      reinterpret_cast<std::uint32_t>(_bootloader_size),
                      server_address, server_port);
      break;
    }
    case Command::kGetPanelStates:
      sendto(kCommandSocket, Panel::GetPanelStates().data(), 2,
             server_address.data(), server_port);
      break;
    default:
      break;
  }
}

void Network::SendFirmwareCrc(const std::uint32_t pBuffer[],
                              const std::uint32_t &size,
                              const std::array<std::uint8_t, 4> &server_address,
                              const std::uint16_t &server_port) {
  /* __REV, for endianness fix, little to big endian
   * ~, negate(crc XOR 0xFFFFFFFF) for standard CRC32
   */
  auto crc{__REV(~HAL_CRC_Calculate(&hcrc, const_cast<std::uint32_t *>(pBuffer),
                                    size / 4u))};

  sendto(kCommandSocket, (std::uint8_t *)&crc, sizeof(crc),
         const_cast<std::uint8_t *>(server_address.data()), server_port);
}
