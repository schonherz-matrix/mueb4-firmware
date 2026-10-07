"""Operator console output and CSV result log."""

import csv
import datetime
import os
import sys
from dataclasses import asdict, dataclass, fields

try:
    import winsound
except ImportError:  # Not on Windows
    winsound = None

GREEN = "\033[42;30m"
RED = "\033[41;97m"
YELLOW = "\033[93m"
RESET = "\033[0m"


@dataclass
class Result:
    timestamp: str = ""
    station: str = ""
    mac: str = ""
    result: str = "FAIL"
    failed_step: str = ""
    error: str = ""
    app_crc: str = ""
    boot_crc: str = ""
    duration_s: str = ""
    retries: int = 0


CSV_FIELDS = [f.name for f in fields(Result)]


class Reporter:
    def __init__(self, csv_path: str, beep: bool):
        self.csv_path = csv_path
        self.beep = beep and winsound is not None
        self.passed = 0
        self.failed = 0
        if os.name == "nt":
            os.system("")  # Enables ANSI escape codes in the Windows console

    def step(self, text: str) -> None:
        now = datetime.datetime.now().strftime("%H:%M:%S")
        print(f"[{now}] {text}", flush=True)

    def progress(self, label: str, pages: int, total: int,
                 keepalives: int) -> None:
        width = 30
        filled = width * pages // total
        bar = "#" * filled + "-" * (width - filled)
        end = "\n" if pages == total else ""
        print(f"\r  {label} [{bar}] {pages}/{total} pages ({keepalives} !)",
              end=end, flush=True)

    def warning(self, text: str) -> None:
        print(f"{YELLOW}{text}{RESET}", flush=True)

    def critical(self, text: str) -> None:
        print(f"{RED} {text} {RESET}", flush=True)

    def record(self, result: Result) -> None:
        if result.result == "PASS":
            self.passed += 1
            self._banner(GREEN, f"PASS  {result.mac}")
            self._beep(1000, 150)
        else:
            self.failed += 1
            self._banner(
                RED, f"FAIL  {result.mac or '?'}  [{result.failed_step}] "
                f"{result.error}")
            self._beep(400, 600)

        print(f"Passed: {self.passed}  Failed: {self.failed}", flush=True)

        new_file = not os.path.exists(self.csv_path)
        with open(self.csv_path, "a", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=CSV_FIELDS)
            if new_file:
                writer.writeheader()
            writer.writerow(asdict(result))

    def _banner(self, color: str, text: str) -> None:
        line = " " * (len(text) + 8)
        print(f"\n{color}{line}\n    {text}    \n{line}{RESET}\n",
              file=sys.stdout, flush=True)

    def _beep(self, frequency: int, duration_ms: int) -> None:
        if self.beep:
            winsound.Beep(frequency, duration_ms)
