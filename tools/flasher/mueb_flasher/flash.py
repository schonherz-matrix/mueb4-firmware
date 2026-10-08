"""TCP firmware transfer to the device's firmware updater."""

import socket
import struct
import threading
import time
import zlib
from typing import Callable

PAGE_SIZE = 1024
KEEPALIVE = b"!"
FLASH_OK = b"FLASH_OK"
FLASH_ERROR = b"FLASH_ERROR"


def pad_image(image: bytes) -> bytes:
    """Pads to whole 32-bit words with erased flash value, the device
    programs and verifies whole words."""
    return image + b"\xff" * (-len(image) % 4)


def image_header(image: bytes) -> bytes:
    """Size and CRC32 (zlib) of the padded image, big endian."""
    return struct.pack(">II", len(image), zlib.crc32(image))


class FlashError(Exception):
    pass


class ConnectError(FlashError):
    pass


def connect(ip: str, port: int, bind_ip: str, window: float) -> socket.socket:
    deadline = time.monotonic() + window
    last_error = None
    while time.monotonic() < deadline:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(1.0)
        try:
            sock.bind((bind_ip, 0))
            sock.connect((ip, port))
            return sock
        except OSError as e:
            last_error = e
            sock.close()
            time.sleep(0.2)
    raise ConnectError(f"cannot connect to {ip}:{port}: {last_error}")


def tcp_flash(image: bytes, ip: str, port: int, bind_ip: str,
              connect_window: float, flash_ok_timeout: float,
              progress: Callable[[int, int, int], None] | None = None,
              header: bool = True) -> None:
    """Sends the image header, then `image` page by page and waits for
    FLASH_OK. The device answers FLASH_ERROR if the header is invalid or the
    flash content doesn't match the CRC32.

    `header=False` is for the old bootloader without image verification, it
    expects the image only.

    `progress(pages_sent, total_pages, keepalives)` is called after each page.
    """
    image = pad_image(image)
    sock = connect(ip, port, bind_ip, connect_window)
    received = bytearray()
    keepalives = 0
    done = threading.Event()

    def reader():
        nonlocal keepalives
        while True:
            try:
                chunk = sock.recv(64)
            except socket.timeout:
                continue
            except OSError:
                break
            if not chunk:
                break
            keepalives += chunk.count(KEEPALIVE)
            received.extend(chunk)
            if FLASH_OK in received or FLASH_ERROR in received:
                break
        done.set()

    thread = threading.Thread(target=reader, daemon=True, name="flash-rx")
    try:
        # Also bounds sendall() if the device stalls
        sock.settimeout(flash_ok_timeout)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        thread.start()

        if header:
            sock.sendall(image_header(image))

        total = (len(image) + PAGE_SIZE - 1) // PAGE_SIZE
        for page in range(total):
            if FLASH_ERROR in received:
                raise FlashError("device rejected the image header")
            if done.is_set():
                raise FlashError("connection closed by device during transfer")
            sock.sendall(image[page * PAGE_SIZE:(page + 1) * PAGE_SIZE])
            if progress:
                progress(page + 1, total, keepalives)

        # End of image, the device finishes the flash on FIN
        sock.shutdown(socket.SHUT_WR)

        if not done.wait(flash_ok_timeout):
            raise FlashError("no FLASH_OK from device")
        if FLASH_ERROR in received:
            raise FlashError("device verification failed (size or CRC32)")
        if FLASH_OK not in received:
            raise FlashError("no FLASH_OK from device")
    except OSError as e:
        raise FlashError(f"transfer failed: {e}") from e
    finally:
        sock.close()
        thread.join(1.0)
