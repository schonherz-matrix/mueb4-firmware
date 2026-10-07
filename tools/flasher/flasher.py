"""MUEB4 network flashing station.

Plug a board into the station NIC; the application and the bootloader
firmware are flashed and verified automatically, then the next board can be
plugged in.
"""

import socket
import sys
import zlib
from pathlib import Path

from mueb_flasher.commands import CommandClient
from mueb_flasher.config import Config, parse_args
from mueb_flasher.dhcp import SERVER_PORT, DhcpServer, same_subnet
from mueb_flasher.link import LinkMonitor
from mueb_flasher.report import Reporter
from mueb_flasher.station import Station


def load_image(path: str) -> bytes:
    try:
        return Path(path).read_bytes()
    except OSError as e:
        sys.exit(f"Cannot read {path}: {e}")


def check_station(cfg: Config, link: LinkMonitor) -> None:
    if not link.exists():
        sys.exit(f'Interface "{cfg.iface}" not found')

    if not same_subnet(cfg.host_ip, cfg.device_ip, cfg.netmask):
        sys.exit(f"{cfg.host_ip} and {cfg.device_ip} are not in the same "
                 f"subnet ({cfg.netmask})")

    if not link.is_up():
        print("Link is down, interface address and DHCP port are checked "
              "once a board is plugged in")
        return

    if not link.has_ip(cfg.host_ip):
        sys.exit(f'Interface "{cfg.iface}" does not have {cfg.host_ip}, '
                 "set it as static IP")

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        try:
            sock.bind((cfg.host_ip, SERVER_PORT))
        except OSError as e:
            sys.exit(f"UDP port {SERVER_PORT} is in use (another DHCP server, "
                     f"e.g. Internet Connection Sharing?): {e}")


def main(argv=None) -> None:
    cfg = parse_args(argv)
    app_image = load_image(cfg.app_bin)
    boot_image = load_image(cfg.boot_bin)

    print(f"Station:    {cfg.station_id} on \"{cfg.iface}\" "
          f"({cfg.host_ip} -> {cfg.device_ip})")
    print(f"App:        {cfg.app_bin}  {len(app_image)} bytes  "
          f"CRC32 0x{zlib.crc32(app_image):08X}")
    print(f"Bootloader: {cfg.boot_bin}  {len(boot_image)} bytes  "
          f"CRC32 0x{zlib.crc32(boot_image):08X}")
    print(f"Results:    {cfg.results_csv}\n")

    link = LinkMonitor(cfg.iface)
    check_station(cfg, link)

    reporter = Reporter(cfg.results_csv, cfg.beep)
    dhcp = DhcpServer(cfg.host_ip, cfg.device_ip, cfg.netmask, cfg.lease_time,
                      link.is_up, reporter.step)
    dhcp.start()

    command_client = CommandClient(cfg.device_ip, cfg.command_port,
                                   cfg.host_ip, cfg.command_timeout,
                                   cfg.command_retries)
    station = Station(cfg, link, dhcp, command_client, reporter, app_image,
                      boot_image)
    try:
        station.run_forever()
    except KeyboardInterrupt:
        print(f"\nStopped. Passed: {reporter.passed}  "
              f"Failed: {reporter.failed}")


if __name__ == "__main__":
    main()
