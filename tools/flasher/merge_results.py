"""Merges the results.csv files of several flashing stations.

Usage: python merge_results.py station1.csv station2.csv ... [-o merged.csv]
"""

import argparse
import csv
from collections import defaultdict

from mueb_flasher.report import CSV_FIELDS


def main(argv=None) -> None:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("inputs", nargs="+", help="per-station results CSV files")
    p.add_argument("-o", "--output", default="merged_results.csv")
    args = p.parse_args(argv)

    rows = []
    for path in args.inputs:
        with open(path, newline="", encoding="utf-8") as f:
            rows.extend(csv.DictReader(f))
    rows.sort(key=lambda row: row["timestamp"])

    with open(args.output, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=CSV_FIELDS,
                                extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)

    by_mac = defaultdict(list)
    for row in rows:
        if row["mac"]:
            by_mac[row["mac"]].append(row)

    passed = {mac for mac, runs in by_mac.items()
              if any(r["result"] == "PASS" for r in runs)}
    never_passed = sorted(set(by_mac) - passed)
    repeated = sorted(mac for mac, runs in by_mac.items() if len(runs) > 1)
    no_mac = sum(1 for row in rows if not row["mac"])

    print(f"Runs: {len(rows)}  Boards: {len(by_mac)}  Passed: {len(passed)}  "
          f"Never passed: {len(never_passed)}  Runs without MAC: {no_mac}")
    for mac in never_passed:
        last = by_mac[mac][-1]
        print(f"  NEVER PASSED {mac}  last: [{last['failed_step']}] "
              f"{last['error']} ({last['station']})")
    for mac in repeated:
        stations = ", ".join(sorted({r["station"] for r in by_mac[mac]}))
        print(f"  FLASHED {len(by_mac[mac])}x {mac}  ({stations})")
    print(f"Merged into {args.output}")


if __name__ == "__main__":
    main()
