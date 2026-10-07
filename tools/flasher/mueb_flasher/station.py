"""Per-device flashing flow and the endless station loop."""

import datetime
import queue
import time
import zlib

from . import commands
from .config import Config
from .flash import ConnectError, FlashError, tcp_flash
from .report import Reporter, Result


class StepFailed(Exception):
    def __init__(self, step: str, message: str):
        super().__init__(f"{step}: {message}")
        self.step = step
        self.message = message


class Station:
    """Flashes application then bootloader firmware to one board at a time.

    `link` needs is_up(), wait_up() and wait_down(debounce); `dhcp` needs an
    `acks` queue of MAC strings and drain().
    """

    def __init__(self, cfg: Config, link, dhcp,
                 command_client: commands.CommandClient, reporter: Reporter,
                 app_image: bytes, boot_image: bytes):
        self.cfg = cfg
        self.link = link
        self.dhcp = dhcp
        self.commands = command_client
        self.reporter = reporter
        self.app_image = app_image
        self.boot_image = boot_image
        self.app_crc = zlib.crc32(app_image)
        self.boot_crc = zlib.crc32(boot_image)
        self._mac: str | None = None
        self._retries = 0

    def run_forever(self) -> None:
        while True:
            self.dhcp.drain()
            self.reporter.step("Waiting for board, plug in the cable...")
            self.link.wait_up()
            self.reporter.record(self.flash_board())
            self.reporter.step("Unplug the board")
            self.link.wait_down(self.cfg.unplug_debounce)

    def flash_board(self) -> Result:
        start = time.monotonic()
        result = Result(
            timestamp=datetime.datetime.now().isoformat(timespec="seconds"),
            station=self.cfg.station_id)
        self._mac = None
        self._retries = 0

        try:
            self._flow(result)
            result.result = "PASS"
        except StepFailed as e:
            result.failed_step = e.step
            result.error = e.message

        result.mac = self._mac or ""
        result.retries = self._retries
        result.duration_s = f"{time.monotonic() - start:.1f}"
        return result

    def _flow(self, result: Result) -> None:
        step = self.reporter.step

        # 1-2. Link up, DHCP ACK
        step("Link up, waiting for DHCP")
        self._mac = self._wait_dhcp_ack(None, "DHCP")
        step(f"Board {self._mac} got {self.cfg.device_ip}")

        # 3. Application running?
        self._wait_app("ping")
        step("Application responds to ping")

        # 4. Enter application firmware update mode, repeat 1-2
        self._enter_bootloader()
        step("Bootloader in firmware update mode")

        # 5-6. Send application, device answers FLASH_OK and restarts
        self._flash_app()
        step("Application flashed (FLASH_OK)")

        # 7. Repeat 1-2, verify application checksum
        self._wait_dhcp_ack(self._mac, "DHCP after app flash")
        self._wait_app("app start after flash")
        crc = self.commands.checksum(commands.GET_FIRMWARE_CHECKSUM)
        result.app_crc = _hex(crc)
        if crc != self.app_crc:
            raise StepFailed("app checksum", f"device {_hex(crc)}, "
                             f"expected {_hex(self.app_crc)}")
        step(f"Application checksum OK ({result.app_crc})")

        # Bootloader: flash and verify checksum
        self._flash_bootloader(result)
        step(f"Bootloader checksum OK ({result.boot_crc})")

    def _wait_dhcp_ack(self, mac: str | None, step: str,
                       timeout: float | None = None,
                       required: bool = True) -> str | None:
        """Waits for a DHCP ACK sent to `mac` (any MAC when None)."""
        timeout = self.cfg.dhcp_timeout if timeout is None else timeout
        deadline = time.monotonic() + timeout
        down_since = None

        while True:
            try:
                got = self.dhcp.acks.get(timeout=0.2)
            except queue.Empty:
                got = None
            if got is not None:
                if mac is None or got == mac:
                    return got
                self.reporter.warning(f"Ignoring DHCP ACK for {got}")

            now = time.monotonic()
            if self.link.is_up():
                down_since = None
            elif down_since is None:
                down_since = now
            elif now - down_since > self.cfg.removed_timeout:
                raise StepFailed(step, "board removed")

            if now > deadline:
                if required:
                    raise StepFailed(step, "no DHCP ACK")
                return None

    def _wait_app(self, step: str) -> None:
        deadline = time.monotonic() + self.cfg.app_boot_timeout
        while time.monotonic() < deadline:
            if self.commands.ping(retries=1):
                return
        raise StepFailed(step, "application does not respond")

    def _enter_bootloader(self) -> None:
        for _ in range(self.cfg.command_retries):
            self.dhcp.drain()
            self.commands.send(commands.START_FIRMWARE_UPDATE)

            if self._wait_dhcp_ack(self._mac, "DHCP in bootloader", 3.0,
                                   required=False):
                return

            # The application still answering means the command was lost
            if self.commands.ping(retries=1):
                self._retries += 1
                self.reporter.warning("Update command lost, resending")
                continue

            self._wait_dhcp_ack(self._mac, "DHCP in bootloader")
            return

        raise StepFailed("enter bootloader",
                         "device ignored kStartFirmwareUpdate")

    def _flash(self, label: str, image: bytes, connect_window: float) -> None:
        tcp_flash(
            image, self.cfg.device_ip, self.cfg.flash_port, self.cfg.host_ip,
            connect_window, self.cfg.flash_ok_timeout,
            lambda pages, total, keepalives: self.reporter.progress(
                label, pages, total, keepalives))

    def _flash_app(self) -> None:
        last_error = None
        for attempt in range(self.cfg.flash_attempts):
            # After a failed attempt the bootloader may restart and redo DHCP
            window = (self.cfg.connect_window if attempt == 0
                      else self.cfg.dhcp_timeout)
            try:
                self._flash("app", self.app_image, window)
                self.dhcp.drain()
                return
            except FlashError as e:
                last_error = e
                self._retries += 1
                self.reporter.warning(f"App flash attempt {attempt + 1} "
                                      f"failed: {e}")
        raise StepFailed("app flash", str(last_error))

    def _flash_bootloader(self, result: Result) -> None:
        last_error = ""
        for attempt in range(self.cfg.flash_attempts):
            if attempt:
                self._retries += 1
                self.reporter.critical(
                    "DO NOT POWER OFF - bootloader may be corrupt, retrying")
                # The application returns from the updater after its timeout
                self._wait_app("bootloader retry")

            self.commands.send(commands.START_BOOTLOADER_FIRMWARE_UPDATE)
            try:
                self._flash("boot", self.boot_image, self.cfg.connect_window)
            except ConnectError as e:
                last_error = str(e)
                self.reporter.warning(f"Bootloader update not started: {e}")
                continue
            except FlashError as e:
                last_error = str(e)
                self.reporter.warning(f"Bootloader flash attempt "
                                      f"{attempt + 1} failed: {e}")
                continue
            self.reporter.step("Bootloader flashed (FLASH_OK)")

            crc = self.commands.checksum(
                commands.GET_BOOTLOADER_FIRMWARE_CHECKSUM)
            result.boot_crc = _hex(crc)
            if crc == self.boot_crc:
                return
            last_error = (f"checksum: device {_hex(crc)}, "
                          f"expected {_hex(self.boot_crc)}")
            self.reporter.warning(f"Bootloader {last_error}")

        self.reporter.critical(
            "DO NOT POWER OFF - bootloader is likely corrupt, needs SWD "
            "recovery if the board is reset")
        raise StepFailed("bootloader", last_error)


def _hex(value: int | None) -> str:
    return "none" if value is None else f"0x{value:08X}"
