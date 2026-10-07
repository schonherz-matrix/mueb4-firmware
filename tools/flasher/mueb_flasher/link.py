"""Ethernet link (cable) detection."""

import time


class LinkMonitor:
    """Polls the operating state of one network interface."""

    def __init__(self, iface: str, poll_interval: float = 0.2):
        import psutil  # Imported here so tests can run without psutil

        self._psutil = psutil
        self.iface = iface
        self.poll_interval = poll_interval

    def exists(self) -> bool:
        return self.iface in self._psutil.net_if_stats()

    def is_up(self) -> bool:
        stats = self._psutil.net_if_stats().get(self.iface)
        return bool(stats and stats.isup)

    def has_ip(self, ip: str) -> bool:
        for addr in self._psutil.net_if_addrs().get(self.iface, []):
            if addr.address == ip:
                return True
        return False

    def wait_up(self, timeout: float | None = None) -> bool:
        deadline = None if timeout is None else time.monotonic() + timeout
        while not self.is_up():
            if deadline is not None and time.monotonic() > deadline:
                return False
            time.sleep(self.poll_interval)
        return True

    def wait_down(self, debounce: float) -> None:
        """Waits until the link has been down for at least `debounce` seconds."""
        down_since = None
        while True:
            if self.is_up():
                down_since = None
            elif down_since is None:
                down_since = time.monotonic()
            elif time.monotonic() - down_since >= debounce:
                return
            time.sleep(self.poll_interval)
