import os
import socket
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from mueb_flasher import dhcp  # noqa: E402

MAC = bytes.fromhex("0004a3123456") + bytes(10)


class FakeSocket:
    def __init__(self):
        self.sent = []

    def sendto(self, data, addr):
        self.sent.append((dhcp.DhcpPacket.parse(data), addr))


def request(message_type, **options):
    opts = {dhcp.OPT_MESSAGE_TYPE: bytes([message_type])}
    for code, ip in options.items():
        opts[int(code[1:])] = socket.inet_aton(ip)
    return dhcp.DhcpPacket(op=dhcp.BOOTREQUEST, xid=0x1234, flags=0x8000,
                           chaddr=MAC, options=opts)


class DhcpPacketTest(unittest.TestCase):
    def test_round_trip(self):
        packet = request(dhcp.REQUEST, o50="10.6.18.5", o54="10.6.0.1")
        parsed = dhcp.DhcpPacket.parse(packet.build())
        self.assertEqual(parsed.xid, 0x1234)
        self.assertEqual(parsed.flags, 0x8000)
        self.assertEqual(parsed.mac, "00:04:a3:12:34:56")
        self.assertEqual(parsed.message_type, dhcp.REQUEST)
        self.assertEqual(parsed.option_ip(dhcp.OPT_REQUESTED_IP), "10.6.18.5")
        self.assertEqual(parsed.option_ip(dhcp.OPT_SERVER_ID), "10.6.0.1")

    def test_rejects_garbage(self):
        with self.assertRaises(ValueError):
            dhcp.DhcpPacket.parse(b"\x01" * 100)


class DhcpServerTest(unittest.TestCase):
    def setUp(self):
        self.server = dhcp.DhcpServer("10.6.0.1", "10.6.18.5", "255.255.0.0",
                                      600, lambda: True, log=lambda _: None)
        self.sock = FakeSocket()
        self.server._sock = self.sock

    def test_discover_gets_offer(self):
        self.server._handle(request(dhcp.DISCOVER))
        reply, addr = self.sock.sent[0]
        self.assertEqual(addr, ("255.255.255.255", 68))
        self.assertEqual(reply.message_type, dhcp.OFFER)
        self.assertEqual(reply.yiaddr, "10.6.18.5")
        self.assertEqual(reply.xid, 0x1234)
        self.assertEqual(reply.option_ip(dhcp.OPT_SERVER_ID), "10.6.0.1")
        self.assertEqual(reply.option_ip(dhcp.OPT_SUBNET_MASK), "255.255.0.0")
        self.assertTrue(self.server.acks.empty())

    def test_request_gets_ack_and_reports_mac(self):
        self.server._handle(
            request(dhcp.REQUEST, o50="10.6.18.5", o54="10.6.0.1"))
        reply, _ = self.sock.sent[0]
        self.assertEqual(reply.message_type, dhcp.ACK)
        self.assertEqual(reply.yiaddr, "10.6.18.5")
        self.assertEqual(self.server.acks.get_nowait(), "00:04:a3:12:34:56")

    def test_request_for_other_ip_gets_nak(self):
        self.server._handle(request(dhcp.REQUEST, o50="192.168.1.10"))
        reply, _ = self.sock.sent[0]
        self.assertEqual(reply.message_type, dhcp.NAK)
        self.assertTrue(self.server.acks.empty())

    def test_request_to_other_server_ignored(self):
        self.server._handle(
            request(dhcp.REQUEST, o50="10.6.18.5", o54="10.6.0.99"))
        self.assertEqual(self.sock.sent, [])

    def test_drain(self):
        self.server.acks.put("aa")
        self.server.drain()
        self.assertTrue(self.server.acks.empty())

    def test_same_subnet(self):
        self.assertTrue(dhcp.same_subnet("10.6.0.1", "10.6.18.5",
                                         "255.255.0.0"))
        self.assertFalse(dhcp.same_subnet("10.6.0.1", "10.6.18.5",
                                          "255.255.255.0"))


if __name__ == "__main__":
    unittest.main()
