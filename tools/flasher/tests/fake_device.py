"""Loopback simulation of a MUEB4 board for testing the station flow.

Implements the UDP command protocol and the TCP firmware updater: the device
sends "!\\0" keepalives, receives 1024-byte pages and answers FLASH_OK once
the host closes its side.
"""

import socket
import struct
import threading
import time
import zlib

from mueb_flasher import commands

PAGE_SIZE = 1024
ACCEPT_TIMEOUT = 10.0


def free_tcp_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class FakeDevice:
    def __init__(self, mac: str, on_dhcp_ack, app: bytes = b"old app",
                 boot: bytes = b"old boot"):
        self.mac = mac
        self.on_dhcp_ack = on_dhcp_ack
        self.app = bytes(app)
        self.boot = bytes(boot)
        self.mode = "app"  # "app", "bootloader" or "busy"
        # Fault injection
        self.fail_app_flashes = 0
        self.fail_boot_flashes = 0
        self.drop_start_commands = 0
        self.corrupt_app = False
        self.respond_to_ping = True

        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp.bind(("127.0.0.1", 0))
        self.udp.settimeout(0.2)
        self.command_port = self.udp.getsockname()[1]
        self.flash_port = free_tcp_port()
        self._stop = threading.Event()
        threading.Thread(target=self._udp_loop, daemon=True).start()

    def close(self) -> None:
        self._stop.set()
        self.udp.close()

    def _dhcp_later(self) -> None:
        threading.Timer(0.1, self.on_dhcp_ack, args=(self.mac,)).start()

    def _udp_loop(self) -> None:
        while not self._stop.is_set():
            try:
                data, addr = self.udp.recvfrom(1500)
            except socket.timeout:
                continue
            except OSError:
                return
            if self.mode != "app" or data[:4] != commands.COMMAND_ID:
                continue  # Bootloader or blocked in updater: no answer

            code = data[4]
            if code == commands.PING and self.respond_to_ping:
                self.udp.sendto(b"pong\0", addr)
            elif code == commands.GET_FIRMWARE_CHECKSUM:
                self.udp.sendto(struct.pack(">I", zlib.crc32(self.app)), addr)
            elif code == commands.GET_BOOTLOADER_FIRMWARE_CHECKSUM:
                self.udp.sendto(struct.pack(">I", zlib.crc32(self.boot)), addr)
            elif code == commands.START_FIRMWARE_UPDATE:
                if self.drop_start_commands:
                    self.drop_start_commands -= 1
                    continue
                self.mode = "bootloader"
                self._dhcp_later()
                threading.Thread(target=self._bootloader_updater,
                                 daemon=True).start()
            elif code == commands.START_BOOTLOADER_FIRMWARE_UPDATE:
                self.mode = "busy"
                threading.Thread(target=self._app_bootloader_updater,
                                 daemon=True).start()

    def _receive_image(self, fail: bool) -> bytes | None:
        """One listen/accept/transfer cycle, None on failure or timeout."""
        with socket.socket() as listener:
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.bind(("127.0.0.1", self.flash_port))
            listener.listen(1)
            listener.settimeout(ACCEPT_TIMEOUT)
            try:
                conn, _ = listener.accept()
            except socket.timeout:
                return None

        with conn:
            image = bytearray()
            while True:
                conn.sendall(b"!\0")  # Keepalive
                page = conn.recv(PAGE_SIZE)
                if not page:
                    break  # Host closed its side: image complete
                if fail:
                    return None  # Drop connection without FLASH_OK
                image += page
            conn.sendall(b"FLASH_OK\0")
            return bytes(image)

    def _bootloader_updater(self) -> None:
        while True:
            fail = self.fail_app_flashes > 0
            self.fail_app_flashes -= fail
            image = self._receive_image(fail)
            if image is not None:
                break
            if not fail:
                return  # Accept timeout (test teardown)
        if self.corrupt_app:
            image = image[:-1] + bytes([image[-1] ^ 0xFF])
        self.app = image
        time.sleep(0.1)
        self.mode = "app"  # Reset into new application
        self._dhcp_later()

    def _app_bootloader_updater(self) -> None:
        fail = self.fail_boot_flashes > 0
        self.fail_boot_flashes -= fail
        image = self._receive_image(fail)
        if image is not None:
            self.boot = image
        self.mode = "app"  # Returns to the application, no reset
