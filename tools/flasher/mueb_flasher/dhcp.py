"""Minimal single-client DHCP server.

Always offers the same IP address. Every ACK it sends is reported on a queue
together with the client's MAC address, so the flashing flow knows when the
device has an address.
"""

import ipaddress
import queue
import socket
import struct
import threading
import time
from dataclasses import dataclass, field
from typing import Callable

SERVER_PORT = 67
CLIENT_PORT = 68
MAGIC_COOKIE = b"\x63\x82\x53\x63"

BOOTREQUEST = 1
BOOTREPLY = 2

# Message types (option 53)
DISCOVER = 1
OFFER = 2
REQUEST = 3
DECLINE = 4
ACK = 5
NAK = 6
RELEASE = 7
INFORM = 8

# Options
OPT_PAD = 0
OPT_SUBNET_MASK = 1
OPT_REQUESTED_IP = 50
OPT_LEASE_TIME = 51
OPT_MESSAGE_TYPE = 53
OPT_SERVER_ID = 54
OPT_RENEWAL_TIME = 58
OPT_REBINDING_TIME = 59
OPT_END = 255

# op, htype, hlen, hops, xid, secs, flags, ciaddr, yiaddr, siaddr, giaddr,
# chaddr, sname, file
_HEADER = struct.Struct("!BBBBIHH4s4s4s4s16s64s128s")


@dataclass
class DhcpPacket:
    op: int = BOOTREPLY
    htype: int = 1
    hlen: int = 6
    hops: int = 0
    xid: int = 0
    secs: int = 0
    flags: int = 0
    ciaddr: str = "0.0.0.0"
    yiaddr: str = "0.0.0.0"
    siaddr: str = "0.0.0.0"
    giaddr: str = "0.0.0.0"
    chaddr: bytes = bytes(16)
    options: dict[int, bytes] = field(default_factory=dict)

    @property
    def mac(self) -> str:
        return ":".join(f"{b:02x}" for b in self.chaddr[:self.hlen])

    @property
    def message_type(self) -> int | None:
        value = self.options.get(OPT_MESSAGE_TYPE)
        return value[0] if value else None

    def option_ip(self, code: int) -> str | None:
        value = self.options.get(code)
        if value is None or len(value) != 4:
            return None
        return socket.inet_ntoa(value)

    @classmethod
    def parse(cls, data: bytes) -> "DhcpPacket":
        if len(data) < _HEADER.size + len(MAGIC_COOKIE):
            raise ValueError("packet too short")
        (op, htype, hlen, hops, xid, secs, flags, ciaddr, yiaddr, siaddr,
         giaddr, chaddr, _sname, _file) = _HEADER.unpack_from(data)
        if data[_HEADER.size:_HEADER.size + 4] != MAGIC_COOKIE:
            raise ValueError("bad magic cookie")

        options = {}
        i = _HEADER.size + 4
        while i < len(data):
            code = data[i]
            if code == OPT_END:
                break
            if code == OPT_PAD:
                i += 1
                continue
            if i + 1 >= len(data):
                raise ValueError("truncated option")
            length = data[i + 1]
            options[code] = data[i + 2:i + 2 + length]
            i += 2 + length

        return cls(op, htype, hlen, hops, xid, secs, flags,
                   socket.inet_ntoa(ciaddr), socket.inet_ntoa(yiaddr),
                   socket.inet_ntoa(siaddr), socket.inet_ntoa(giaddr), chaddr,
                   options)

    def build(self) -> bytes:
        header = _HEADER.pack(self.op, self.htype, self.hlen, self.hops,
                              self.xid, self.secs, self.flags,
                              socket.inet_aton(self.ciaddr),
                              socket.inet_aton(self.yiaddr),
                              socket.inet_aton(self.siaddr),
                              socket.inet_aton(self.giaddr), self.chaddr,
                              bytes(64), bytes(128))
        options = b"".join(
            bytes([code, len(value)]) + value
            for code, value in self.options.items())
        return header + MAGIC_COOKIE + options + bytes([OPT_END])


class DhcpServer(threading.Thread):
    """Serves one fixed address on the station interface.

    The socket is (re)bound whenever the link comes up, because Windows
    removes the static address from the interface while the cable is
    unplugged.
    """

    def __init__(self, server_ip: str, offer_ip: str, netmask: str,
                 lease_time: int, is_link_up: Callable[[], bool],
                 log: Callable[[str], None] = print):
        super().__init__(daemon=True, name="dhcp")
        self.server_ip = server_ip
        self.offer_ip = offer_ip
        self.netmask = netmask
        self.lease_time = lease_time
        self.is_link_up = is_link_up
        self.log = log
        self.acks: queue.Queue[str] = queue.Queue()
        self._sock: socket.socket | None = None
        self._stop = threading.Event()

    def drain(self) -> None:
        """Forgets ACKs that were sent before this call."""
        while True:
            try:
                self.acks.get_nowait()
            except queue.Empty:
                return

    def stop(self) -> None:
        self._stop.set()

    def run(self) -> None:
        while not self._stop.is_set():
            if not self.is_link_up():
                self._close()
                time.sleep(0.2)
                continue

            if self._sock is None and not self._open():
                time.sleep(0.5)
                continue

            try:
                data, _ = self._sock.recvfrom(1500)
            except socket.timeout:
                continue
            except OSError:
                self._close()
                continue

            try:
                self._handle(DhcpPacket.parse(data))
            except ValueError:
                continue
            except OSError:
                self._close()

    def _open(self) -> bool:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM,
                             socket.IPPROTO_UDP)
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            # On Windows a socket bound to the interface address also
            # receives the broadcasts arriving on that interface, and
            # broadcasts sent from it leave through that interface.
            sock.bind((self.server_ip, SERVER_PORT))
            sock.settimeout(0.5)
        except OSError:
            # Address not assigned yet (link just came up), retry later
            sock.close()
            return False
        self._sock = sock
        return True

    def _close(self) -> None:
        if self._sock is not None:
            self._sock.close()
            self._sock = None

    def _handle(self, request: DhcpPacket) -> None:
        if request.op != BOOTREQUEST:
            return

        message_type = request.message_type
        if message_type == DISCOVER:
            self._reply(request, OFFER)
        elif message_type == REQUEST:
            server_id = request.option_ip(OPT_SERVER_ID)
            if server_id is not None and server_id != self.server_ip:
                return  # Client selected another server

            requested = request.option_ip(OPT_REQUESTED_IP) or request.ciaddr
            if requested == self.offer_ip:
                self._reply(request, ACK)
                self.log(f"DHCP ACK {self.offer_ip} -> {request.mac}")
                self.acks.put(request.mac)
            else:
                self._reply(request, NAK)

    def _reply(self, request: DhcpPacket, message_type: int) -> None:
        options = {
            OPT_MESSAGE_TYPE: bytes([message_type]),
            OPT_SERVER_ID: socket.inet_aton(self.server_ip),
        }
        yiaddr = "0.0.0.0"
        if message_type != NAK:
            yiaddr = self.offer_ip
            options[OPT_LEASE_TIME] = struct.pack("!I", self.lease_time)
            options[OPT_RENEWAL_TIME] = struct.pack("!I", self.lease_time // 2)
            options[OPT_REBINDING_TIME] = struct.pack(
                "!I", self.lease_time * 7 // 8)
            options[OPT_SUBNET_MASK] = socket.inet_aton(self.netmask)

        reply = DhcpPacket(op=BOOTREPLY, htype=request.htype,
                           hlen=request.hlen, xid=request.xid,
                           flags=request.flags, yiaddr=yiaddr,
                           siaddr=self.server_ip, chaddr=request.chaddr,
                           options=options)
        self._sock.sendto(reply.build(), ("255.255.255.255", CLIENT_PORT))


def same_subnet(a: str, b: str, netmask: str) -> bool:
    network = ipaddress.IPv4Network(f"{a}/{netmask}", strict=False)
    return ipaddress.IPv4Address(b) in network
