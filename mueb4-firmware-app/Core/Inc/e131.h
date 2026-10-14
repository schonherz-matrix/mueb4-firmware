#ifndef MUEB4_FIRMWARE_CORE_INC_E131_H_
#define MUEB4_FIRMWARE_CORE_INC_E131_H_

#include <array>
#include <boost/endian.hpp>
#include <boost/static_assert.hpp>
#include <cstdint>

constexpr std::uint32_t VECTOR_ROOT_E131_DATA{0x00000004u};
constexpr std::uint32_t VECTOR_ROOT_E131_EXTENDED{0x00000008u};
constexpr std::uint32_t VECTOR_DMP_SET_PROPERTY{0x02u};
constexpr std::uint32_t VECTOR_E131_DATA_PACKET{0x00000002u};
constexpr std::uint32_t VECTOR_E131_EXTENDED_SYNCHRONIZATION{0x00000001u};
constexpr std::uint16_t ACN_SDT_MULTICAST_PORT{5568u};

constexpr std::array<std::uint8_t, 12u> kAcnPacketIdentifier{
    0x41u, 0x53u, 0x43u, 0x2du, 0x45u, 0x31u,
    0x2eu, 0x31u, 0x37u, 0x00u, 0x00u, 0x00u};

struct RootLayer {
  const boost::endian::big_uint16_t
      preamble_size; /* Define RLP Preamble Size. */
  const boost::endian::big_uint16_t post_amble_size; /* RLP Post-amble Size. */
  const std::array<std::uint8_t, 12u>
      acn_packet_identifier; /* Identifies this packet as E1.17 */
  const boost::endian::big_uint16_t
      flags_and_length; /* Protocol flags and length */
  const boost::endian::big_uint32_t
      vector; /* Identifies RLP Data as 1.31 Protocol PDU */
  const std::array<std::uint8_t, 16u> cid; /* Sender's CID */
};

BOOST_STATIC_ASSERT(sizeof(RootLayer) == 38u);

struct E131DataPacket {
  const RootLayer root_layer;

  struct FramingLayer {
    const boost::endian::big_uint16_t
        flags_and_length; /* Protocol flags and length */
    const boost::endian::big_uint32_t
        vector; /* Identifies 1.31 data as DMP Protocol PDU */
    const std::array<std::uint8_t, 64u>
        source_name;             /* User Assigned Name of Source */
    const std::uint8_t priority; /* Data priority if multiple sources */
    const boost::endian::big_uint16_t
        synchronization_address;                /* Universe address on
                               which sync packets will be sent */
    const std::uint8_t sequence_number;         /* Sequence Number */
    const std::uint8_t options;                 /* Options Flags */
    const boost::endian::big_uint16_t universe; /* Universe Number */
  };
  const FramingLayer framing_layer;

  struct DmpLayer {
    const boost::endian::big_uint16_t
        flags_and_length;      /* Protocol flags and length */
    const std::uint8_t vector; /* Identifies DMP Set Property Message PDU */
    const std::uint8_t address_type_and_data_type; /* Identifies format of
                                                address and data */
    const boost::endian::big_uint16_t
        first_property_address; /* Indicates DMX512-A START Code is
                           at DMP address 0 */
    const boost::endian::big_uint16_t
        address_increment; /* Indicates each property is 1 octet */
    const boost::endian::big_uint16_t
        property_value_count; /* Indicates 1+ the number of slots in packet */
    const std::uint8_t start_code;
    const std::array<std::uint8_t, 512u> /* DMX512-A START Code */
        property_values;                 /* DMX512-A data */
  };

  DmpLayer dmp_layer;
};

BOOST_STATIC_ASSERT(sizeof(E131DataPacket) == sizeof(RootLayer) + 600u);

struct E131SyncPacket {
  const RootLayer root_layer;

  struct FramingLayer {
    const boost::endian::big_uint16_t
        flags_and_length; /* Protocol flags and length */
    const boost::endian::big_uint32_t
        vector; /* Identifies 1.31 data as DMP Protocol PDU */
    const std::uint8_t sequence_number; /* Sequence Number */
    const boost::endian::big_uint16_t
        synchronization_address;                /* Universe Number */
    const boost::endian::big_uint16_t reserved; /* Reserved */
  };

  const FramingLayer framing_layer;
};

BOOST_STATIC_ASSERT(sizeof(E131SyncPacket) == sizeof(RootLayer) + 11u);

#endif  // MUEB4_FIRMWARE_CORE_INC_E131_H_
