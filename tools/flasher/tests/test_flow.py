import os
import queue
import random
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.dirname(__file__))

from fake_device import FakeDevice  # noqa: E402
from mueb_flasher.commands import CommandClient  # noqa: E402
from mueb_flasher.config import Config  # noqa: E402
from mueb_flasher.report import Reporter  # noqa: E402
from mueb_flasher.station import Station  # noqa: E402

MAC = "00:04:a3:12:34:56"


class FakeLink:
    def is_up(self):
        return True


class FakeDhcp:
    def __init__(self):
        self.acks = queue.Queue()

    def drain(self):
        while not self.acks.empty():
            self.acks.get_nowait()


class QuietReporter(Reporter):
    def step(self, text):
        pass

    def progress(self, *args):
        pass

    def warning(self, text):
        pass

    def critical(self, text):
        pass


class FlowTest(unittest.TestCase):
    def setUp(self):
        rng = random.Random(1)
        self.app = rng.randbytes(10 * 1024 + 300)
        self.boot = rng.randbytes(5 * 1024)
        self.dhcp = FakeDhcp()
        self.device = FakeDevice(MAC, self.dhcp.acks.put)
        self.cfg = Config(
            host_ip="127.0.0.1", device_ip="127.0.0.1", station_id="test",
            command_port=self.device.command_port,
            flash_port=self.device.flash_port, dhcp_timeout=5.0,
            command_timeout=0.3, app_boot_timeout=3.0, connect_window=3.0,
            flash_ok_timeout=3.0, beep=False)
        self.tmp = tempfile.TemporaryDirectory()
        self.reporter = QuietReporter(os.path.join(self.tmp.name, "r.csv"),
                                      False)
        self.client = CommandClient(self.cfg.device_ip, self.cfg.command_port,
                                    self.cfg.host_ip, self.cfg.command_timeout,
                                    self.cfg.command_retries)
        self.make_station()

    def make_station(self):
        self.station = Station(self.cfg, FakeLink(), self.dhcp, self.client,
                               self.reporter, self.app, self.boot)

    def tearDown(self):
        self.device.close()
        self.tmp.cleanup()

    def run_board(self):
        # The board is plugged in and receives its first DHCP ACK
        self.dhcp.acks.put(MAC)
        return self.station.flash_board()

    def test_pass(self):
        result = self.run_board()
        self.assertEqual(result.result, "PASS", result.error)
        self.assertEqual(result.mac, MAC)
        self.assertEqual(self.device.app, self.app)
        self.assertEqual(self.device.boot, self.boot)
        self.assertEqual(result.app_crc, f"0x{zlib.crc32(self.app):08X}")
        self.assertEqual(result.boot_crc, f"0x{zlib.crc32(self.boot):08X}")
        self.assertEqual(result.retries, 0)

    def test_app_flash_retry(self):
        self.device.fail_app_flashes = 1
        result = self.run_board()
        self.assertEqual(result.result, "PASS", result.error)
        self.assertEqual(result.retries, 1)
        self.assertEqual(self.device.app, self.app)

    def test_bootloader_flash_retry(self):
        self.device.fail_boot_flashes = 1
        result = self.run_board()
        self.assertEqual(result.result, "PASS", result.error)
        self.assertEqual(result.retries, 1)
        self.assertEqual(self.device.boot, self.boot)

    def test_app_verification_error_retry(self):
        self.device.corrupt_transfers = 1
        result = self.run_board()
        self.assertEqual(result.result, "PASS", result.error)
        self.assertEqual(result.retries, 1)
        self.assertEqual(self.device.app, self.app)

    def test_bootloader_verification_error_retry(self):
        # App transfer succeeds, the first bootloader transfer is corrupted
        self.device.fail_boot_flashes = 0
        original = self.device._app_bootloader_updater

        def corrupt_once():
            self.device.corrupt_transfers = 1
            original()

        self.device._app_bootloader_updater = corrupt_once
        result = self.run_board()
        self.assertEqual(result.result, "PASS", result.error)
        self.assertEqual(result.retries, 1)
        self.assertEqual(self.device.boot, self.boot)

    def test_unaligned_image_padded(self):
        self.app = self.app + b"\x01"  # Not a multiple of 4
        self.make_station()
        result = self.run_board()
        self.assertEqual(result.result, "PASS", result.error)
        self.assertEqual(self.device.app, self.app + b"\xff\xff\xff")

    def test_legacy_bootloader_upgrade(self):
        # Old board: the app goes to the old bootloader without header, the
        # bootloader to the new app with header
        self.device.legacy_bootloader = True
        self.cfg.legacy_bootloader = True
        result = self.run_board()
        self.assertEqual(result.result, "PASS", result.error)
        self.assertEqual(self.device.app, self.app)
        self.assertEqual(self.device.boot, self.boot)
        self.assertFalse(self.device.legacy_bootloader)

        # Upgraded board works without the flag
        self.cfg.legacy_bootloader = False
        result = self.run_board()
        self.assertEqual(result.result, "PASS", result.error)
        self.assertEqual(result.retries, 0)

    def test_legacy_bootloader_without_flag(self):
        # The old bootloader writes the header into flash
        self.device.legacy_bootloader = True
        result = self.run_board()
        self.assertEqual(result.result, "FAIL")
        self.assertEqual(result.failed_step, "app checksum")

    def test_legacy_flag_with_new_bootloader(self):
        # The new bootloader rejects an image without header
        self.cfg.legacy_bootloader = True
        result = self.run_board()
        self.assertEqual(result.result, "FAIL")
        self.assertEqual(result.failed_step, "app flash")

    def test_lost_start_command_resent(self):
        self.device.drop_start_commands = 1
        result = self.run_board()
        self.assertEqual(result.result, "PASS", result.error)
        self.assertEqual(result.retries, 1)

    def test_app_checksum_mismatch(self):
        self.device.corrupt_app = True
        result = self.run_board()
        self.assertEqual(result.result, "FAIL")
        self.assertEqual(result.failed_step, "app checksum")

    def test_app_not_responding(self):
        self.device.respond_to_ping = False
        result = self.run_board()
        self.assertEqual(result.result, "FAIL")
        self.assertEqual(result.failed_step, "ping")


if __name__ == "__main__":
    unittest.main()
