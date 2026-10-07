"""MUEB UDP command protocol (see mueb4-firmware-app/Core/Inc/command.h)."""

import socket
import struct
import time

COMMAND_ID = b"SEM\0"

START_FIRMWARE_UPDATE = 0x08
PING = 0x0B
GET_FIRMWARE_CHECKSUM = 0x0E
START_BOOTLOADER_FIRMWARE_UPDATE = 0x10
GET_BOOTLOADER_FIRMWARE_CHECKSUM = 0x11


def build_command(code: int) -> bytes:
    # Only the id and the code are mandatory, the rest of the header is zero
    return COMMAND_ID + bytes([code])


class CommandClient:
    def __init__(self, device_ip: str, port: int, bind_ip: str,
                 timeout: float, retries: int):
        self.device_ip = device_ip
        self.port = port
        self.bind_ip = bind_ip
        self.timeout = timeout
        self.retries = retries

    def _socket(self) -> socket.socket:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        # Windows reports ICMP port unreachable as an error on the next
        # recvfrom(), which happens while the device runs the bootloader.
        if hasattr(socket, "SIO_UDP_CONNRESET"):
            sock.ioctl(socket.SIO_UDP_CONNRESET, False)
        sock.bind((self.bind_ip, 0))
        sock.settimeout(self.timeout)
        return sock

    def send(self, code: int) -> None:
        """Sends a command that has no response. Sent exactly once.

        Errors are ignored, the caller detects a lost command.
        """
        try:
            with self._socket() as sock:
                sock.sendto(build_command(code), (self.device_ip, self.port))
        except OSError:
            pass

    def request(self, code: int, retries: int | None = None) -> bytes | None:
        """Sends a command and returns the response, None if none arrived."""
        retries = self.retries if retries is None else retries
        try:
            sock = self._socket()
        except OSError:
            # Host address not available, e.g. link is down
            time.sleep(self.timeout)
            return None
        with sock:
            for _ in range(retries):
                try:
                    sock.sendto(build_command(code),
                                (self.device_ip, self.port))
                except OSError:
                    time.sleep(self.timeout)
                    continue
                deadline = time.monotonic() + self.timeout
                while (remaining := deadline - time.monotonic()) > 0:
                    sock.settimeout(remaining)
                    try:
                        data, (ip, _) = sock.recvfrom(1500)
                    except (socket.timeout, ConnectionResetError):
                        break
                    if ip == self.device_ip:
                        return data
        return None

    def ping(self, retries: int | None = None) -> bool:
        response = self.request(PING, retries)
        return response is not None and response.rstrip(b"\0") == b"pong"

    def checksum(self, code: int) -> int | None:
        response = self.request(code)
        if response is None or len(response) != 4:
            return None
        return struct.unpack(">I", response)[0]
