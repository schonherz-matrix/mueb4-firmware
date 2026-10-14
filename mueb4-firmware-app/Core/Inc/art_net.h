#ifndef MUEB4_FIRMWARE_CORE_INC_ART_NET_H_
#define MUEB4_FIRMWARE_CORE_INC_ART_NET_H_

#include <array>
#include <boost/endian.hpp>
#include <boost/static_assert.hpp>
#include <cstdint>

constexpr std::uint16_t kArtNetPort{0x1936u};
constexpr std::array<std::uint8_t, 8u> kArtNetId{"Art-Net"};
constexpr std::uint16_t kOpPoll{0x2000u}; /* This is an ArtPoll packet, no other
data is contained in this UDP packet */
constexpr std::uint16_t kOpPollReply{0x2100u}; /* This is an ArtPollReply
Packet. It contains device status information */
constexpr std::uint16_t kOpOutput{0x5000u}; /* This is an ArtDmx data packet. It
contains zero start code DMX512 information for a single Universe. */
constexpr std::uint16_t kOpSync{0x5200u}; /* This is an ArtSync data packet. It
is used to force synchronous transfer of ArtDmx packets to a node’s output. */

struct ArtIdOpCode {
  const std::array<std::uint8_t, 8u>
      id; /* Array of 8 characters, the final character
is a null termination. Value = ‘A’ ‘r’ ‘t’ ‘-‘ ‘N’ ‘e’ ‘t’ 0x00 */
  const boost::endian::little_uint16_t op_code; /* Transmitted low byte first */
};

BOOST_STATIC_ASSERT(sizeof(ArtIdOpCode) == 10u);

struct ArtNetHeader {
  const ArtIdOpCode art_id_op_code;
  const boost::endian::big_uint16_t prot_ver; /* High byte of the Art-Net
                        protocol revision number. Low byte of the Art-Net
                        protocol revision number. Current value 14 */
};

BOOST_STATIC_ASSERT(sizeof(ArtNetHeader) == sizeof(ArtIdOpCode) + 2u);

struct ArtPoll {
  const ArtNetHeader header;
  const std::uint8_t talk_to_me; /* Set behaviour of Node */
  const std::uint8_t priority;   /* The lowest priority of diagnostics message
                              that   should be sent. See Table 5 */
};

BOOST_STATIC_ASSERT(sizeof(ArtPoll) == sizeof(ArtNetHeader) + 2u);

struct ArtPollReply {
  const ArtIdOpCode art_id_op_code{kArtNetId, kOpPollReply};
  std::array<std::uint8_t, 4u> ip_address{0u}; /* Array containing the
Node’s IP address. First array entry is most significant byte of address. When
binding is implemented, bound nodes may share the root node’s IP Address and the
BindIndex is used to differentiate the nodes */
  const boost::endian::little_uint16_t port{kArtNetPort};  /* The Port is always
 0x1936 Transmitted low byte first */
  const boost::endian::big_uint16_t version_info{0x0400u}; /* High byte of
Node’s firmware   revision number. The Controller should only use this field to
decide if a   firmware update should proceed. The convention is that a higher
number is a more   recent release of firmware.  Low byte of Node’s firmware
revision number*/
  const std::uint8_t net_switch{0u}; /* Bits 14-8 of the 15 bit Port-Address are
                      encoded into the bottom 7 bits of this field. This is used
                      in combination with SubSwitch and SwIn[] or SwOut[] to
                      produce the full universe address */
  const std::uint8_t sub_switch{0u}; /* Bits 7-4 of the 15 bit Port-Address are
encoded into the bottom 4 bits of this field.
This is used in combination with NetSwitch
and SwIn[] or SwOut[] to produce the full
universe address */
  const boost::endian::big_uint16_t oem{0x2BAFu}; /* The high byte of the Oem
value. The low byte of the Oem value. The Oem word describes the equipment
vendor and the feature set available. Bit 15 high indicates extended features
available. Current registered codes are defined in Table 2 */
  const std::uint8_t ubea_version{0u};      /* This field contains the firmware
     version of the User Bios Extension Area (UBEA). If the UBEA is not programmed,
     this field contains zero */
  const std::uint8_t status_1{0b00100000u}; /* General Status register
      containing bit fields as follows */
  const boost::endian::little_uint16_t esta_man{
      0x7ff0u}; /* The ESTA manufacturer
code. These codes are used to represent equipment manufacturer. They are
assigned by ESTA. This field can be interpreted as two ASCII
bytes representing the manufacturer initials. Hi byte of above. */
  const std::array<std::uint8_t, 18u> short_name{
      "MUEB 4"}; /* The array represents a null
terminated short name for the Node. The Controller uses the ArtAddress packet to
program this string. Max length is 17 characters plus the null. This is a fixed
length field, although the string it contains can be shorter than the field*/
  const std::array<std::uint8_t, 64u> long_name{
      "MUEB 4 SEM & KSZK forever 2022"}; /* The array represents a null
terminated long  name for the Node. The Controller uses the
ArtAddress packet to program this string.
Max length is 63 characters plus the null.
This is a fixed length field, although the
string it contains can be shorter than the
field */
  const std::array<std::uint8_t, 64u> node_report{
      "#0001 [0001] Power On Tests successful."};  /* The array is a textual
 report of   the Node’s operating status or operational errors. It is primarily
 intended for   ‘engineering’ data rather than ‘end user’ data. The field is
 formatted as:   “#xxxx [yyyy..] zzzzz…” xxxx is a hex status code as defined in
 Table
 3. yyyy is a decimal counter that increments
 every time the Node sends an
 ArtPollResponse.
 This allows the controller to monitor event
 changes in the Node.
 zzzz is an English text string defining the
 status.
 This is a fixed length field, although the
 string it contains can be shorter than the
 field */
  const boost::endian::big_uint16_t num_ports{1u}; /* The high byte of the word
 describing the number of input or output ports. The high byte is for future
 expansion and is currently zero.  The low byte of the word describing the
 number of input or output ports. If number of inputs is not equal to number of
 outputs, the largest value is taken. Zero is a legal value if no input or
 output ports are implemented. The maximum value is 4. Nodes can ignore this
 field as the information is implicit in PortTypes[]*/
  const std::array<std::uint8_t, 4u> port_types{
      0b10000000u}; /* Output port, DMX512. This array defines the
operation and protocol of each channel. (A product with 4 inputs and 4 outputs
would report 0xc0, 0xc0, 0xc0, 0xc0). The array length is fixed, independent of
the number of inputs or outputs physically available on the Node */
  const boost::endian::big_uint32_t good_input{
      0u}; /* This array defines input status of the node */
  const boost::endian::big_uint32_t good_output_a{
      0u}; /* This array defines output status of the node */
  const std::array<std::uint8_t, 4u> sw_in{
      0u}; /* Bits 3-0 of the 15 bit Port-Address for
each  of the 4 possible input ports are encoded  into the low nibble */
  std::array<std::uint8_t, 4u> sw_out{
      1u}; /* Bits 3-0 of the 15 bit Port-Address
for each of the 4 possible output ports are encoded into the low nibble.
Controllers unicast ArtDmx to the universes listed here */
  const std::uint8_t sw_video{0u}; /* Set to 00 when video display is showing
                 local data. Set to 01 when video is showing
                 ethernet data. The field is now deprecated */
  const std::uint8_t sw_macro{0u}; /* If the Node supports macro key inputs,
                 this byte represents the  trigger values. The Node is
                 responsible for ‘debouncing’ inputs. When the  ArtPollReply is
                 set to transmit automatically, (TalkToMe Bit 1), the
                 ArtPollReply will be sent on both key down  and key up events.
                 However, the Controller should not assume that only one bit
                 position has changed. The Macro inputs are used for remote
                 event triggering or cueing. Bit fields are active high */
  const std::uint8_t sw_remote{
      0u}; /* If the Node supports remote trigger
inputs,   this byte represents the trigger values. The   Node is
responsible   for ‘debouncing’ inputs.   When the ArtPollReply is set to
transmit   automatically, (TalkToMe Bit 1), the   ArtPollReply will be sent on
both key down   and key up events. However, the Controller   should not assume
that only one bit position   has changed.   The Remote inputs are used for
remote   event triggering or cueing.   Bit fields are active high */
  const boost::endian::big_uint24_t spare{0u}; /* Not used, set to zero */
  const std::uint8_t style{0u}; /* The Style code defines the equipment style
                   of the device. See Table 4 for current Style
                   codes */
  std::array<std::uint8_t, 6u> mac{0u}; /* MAC Address Hi Byte. Set to zero if
node   cannot supply this information */
  std::array<std::uint8_t, 4u> bind_ip{0u}; /* If this unit is part of a
larger or modular product, this is the IP of the root device */
  const std::uint8_t bind_index{1u}; /* This number represents the order of
              bound devices. A lower number means closer to
              root device. A value of 1 means root device */
  const std::uint8_t status_2{0b00011110u};
  const std::array<std::uint8_t, 4u> good_output_b{
      0b01000000u}; /* This array defines output status of the node */
  const std::uint8_t status_3{0u}; /* General Status register
                               containing bit fields as follows */
  const std::array<std::uint8_t, 6u> default_resp_uid{
      0u}; /* RDMnet & LLRP Default Responder UID */
  const boost::endian::big_uint16_t user{
      0u}; /* Available for user specific data */
  const boost::endian::big_uint16_t refresh_rate{
      50u}; /* Maximum refresh rate in Hz at which ArtDmx can be processed,
0 to 44 means the DMX512 rate of 44 Hz. The controller sends 50 fps */
  const std::uint8_t background_queue_policy{
      0u}; /* Only used when Status3 bit 1 is set */
  const std::array<std::uint8_t, 10u> filler{
      0u}; /* Transmit as zero. For future expansion */
};

BOOST_STATIC_ASSERT(sizeof(ArtPollReply) == sizeof(ArtIdOpCode) + 229u);

/// ArtDmx header, the DMX512 data (length bytes) follows it in the packet.
struct ArtDmx {
  const ArtNetHeader header;
  const std::uint8_t sequence; /* The sequence number is used to ensure that
ArtDmx packets are used in the correct order.
When Art-Net is carried over a medium such as
the Internet, it is possible that ArtDmx packets
will reach the receiver out of order.
This field is incremented in the range 0x01 to
0xff to allow the receiving node to resequence
packets.
The Sequence field is set to 0x00 to disable this
feature. */
  const std::uint8_t physical; /* The physical input port from which DMX512 data
was input. This field is for information only. Use Universe for data routing */
  const std::uint8_t sub_uni;  /* The low byte of the 15 bit Port-Address to
   which this packet is destined */
  const std::uint8_t net;      /* The top 7 bits of the 15 bit Port-Address to
        which this packet is destined*/
  const boost::endian::big_uint16_t
      length; /* The length of the DMX512 data array.
This  value should be an even number in the range 2  – 512.  It represents the
number of DMX512 channels  encoded in packet. NB: Products which convert Art-Net
to DMX512 may opt to always send 512  channels.  High Byte. Low Byte of above.
*/
};

BOOST_STATIC_ASSERT(sizeof(ArtDmx) == sizeof(ArtNetHeader) + 6u);

struct ArtSync {
  const ArtNetHeader header;
  const std::uint8_t aux_1; /* Transmit as zero */
  const std::uint8_t aux_2; /* Transmit as zero */
};

BOOST_STATIC_ASSERT(sizeof(ArtSync) == sizeof(ArtNetHeader) + 2u);

#endif  // MUEB4_FIRMWARE_CORE_INC_ART_NET_H_
