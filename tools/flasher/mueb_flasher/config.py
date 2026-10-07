"""Station configuration and command line parsing."""

import argparse
import socket
from dataclasses import dataclass


@dataclass
class Config:
    iface: str = ""
    host_ip: str = "10.6.0.1"
    device_ip: str = "10.6.18.5"
    netmask: str = "255.255.0.0"
    app_bin: str = "mueb4-firmware-app.bin"
    boot_bin: str = "mueb4-firmware-boot.bin"
    station_id: str = socket.gethostname()
    results_csv: str = "results.csv"
    command_port: int = 50000
    flash_port: int = 50002
    lease_time: int = 600
    # Timeouts in seconds
    dhcp_timeout: float = 30.0
    removed_timeout: float = 20.0
    unplug_debounce: float = 2.0
    command_timeout: float = 1.0
    command_retries: int = 3
    app_boot_timeout: float = 15.0
    connect_window: float = 8.0
    flash_ok_timeout: float = 10.0
    flash_attempts: int = 3
    beep: bool = True


def parse_args(argv=None) -> Config:
    d = Config()
    p = argparse.ArgumentParser(
        description="MUEB4 network flashing station: flashes application and "
        "bootloader firmware over Ethernet, one board after another.")
    p.add_argument("--iface", required=True,
                   help='network interface name, e.g. "Ethernet 2"')
    p.add_argument("--host-ip", default=d.host_ip)
    p.add_argument("--device-ip", default=d.device_ip)
    p.add_argument("--netmask", default=d.netmask)
    p.add_argument("--app-bin", default=d.app_bin)
    p.add_argument("--boot-bin", default=d.boot_bin)
    p.add_argument("--station-id", default=d.station_id)
    p.add_argument("--results-csv", default=d.results_csv)
    p.add_argument("--dhcp-timeout", type=float, default=d.dhcp_timeout)
    p.add_argument("--unplug-debounce", type=float, default=d.unplug_debounce)
    p.add_argument("--flash-attempts", type=int, default=d.flash_attempts)
    p.add_argument("--no-beep", dest="beep", action="store_false")
    args = p.parse_args(argv)
    return Config(**{k: v for k, v in vars(args).items()})
