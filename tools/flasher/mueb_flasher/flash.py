"""TCP firmware transfer to the device's firmware updater."""

import socket
import threading
import time
from typing import Callable

PAGE_SIZE = 1024
KEEPALIVE = b"!"
FLASH_OK = b"FLASH_OK"


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
              progress: Callable[[int, int, int], None] | None = None) -> None:
    """Sends `image` page by page and waits for FLASH_OK.

    `progress(pages_sent, total_pages, keepalives)` is called after each page.
    """
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
            if FLASH_OK in received:
                break
        done.set()

    thread = threading.Thread(target=reader, daemon=True, name="flash-rx")
    try:
        # Also bounds sendall() if the device stalls
        sock.settimeout(flash_ok_timeout)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        thread.start()

        total = (len(image) + PAGE_SIZE - 1) // PAGE_SIZE
        for page in range(total):
            if done.is_set():
                raise FlashError("connection closed by device during transfer")
            sock.sendall(image[page * PAGE_SIZE:(page + 1) * PAGE_SIZE])
            if progress:
                progress(page + 1, total, keepalives)

        # End of image, the device finishes the flash on FIN
        sock.shutdown(socket.SHUT_WR)

        if not done.wait(flash_ok_timeout) or FLASH_OK not in received:
            raise FlashError("no FLASH_OK from device")
    except OSError as e:
        raise FlashError(f"transfer failed: {e}") from e
    finally:
        sock.close()
        thread.join(1.0)
