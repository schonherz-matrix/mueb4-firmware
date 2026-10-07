# MUEB4 flashing station

Flashes the application and bootloader firmware to MUEB4 boards over Ethernet,
fully automated, one board after another. The PC and the board are connected
directly with one network cable (no switch).

For every board:

1. Link up, the built-in DHCP server gives the board `10.6.18.5`
2. `kPing` checks that the application is running
3. `kStartFirmwareUpdate`, the board restarts into the bootloader
4. `mueb4-firmware-app.bin` is sent over TCP (port 50002); the board answers `FLASH_OK` and restarts
5. `kGetFirmwareChecksum` is compared with the CRC32 of the file
6. `kStartBootloaderFirmwareUpdate`, then `mueb4-firmware-boot.bin` is sent; the board answers `FLASH_OK`
7. `kGetBootloaderFirmwareChecksum` is compared with the CRC32 of the file
8. PASS/FAIL banner, beep, and a row in `results.csv`. Unplug the board, plug in the next one.

## Station setup (once per PC)

1. Use a dedicated network adapter (e.g. a USB Ethernet adapter) and set it to
   a static IP: `10.6.0.1`, mask `255.255.0.0`, no gateway, no DNS.
   The third octet of the PC address must be `0`, otherwise the board ignores
   the commands.
2. Make sure no other DHCP server runs on that adapter (Internet Connection
   Sharing must be off).
3. Allow the flasher through Windows Firewall on that adapter (needs admin once),
   for example:

   ```bash
   netsh advfirewall firewall add rule name="MUEB flasher" dir=in action=allow protocol=UDP localip=10.6.0.1
   ```

4. Install Python 3.11+ and the dependencies:

   ```bash
   pip install -r requirements.txt
   ```

   Or build a single executable to copy to a station that has no Python:

   ```bash
   pyinstaller --onefile flasher.py
   ```

## Running

Put `mueb4-firmware-app.bin` and `mueb4-firmware-boot.bin` into the working
directory (or pass `--app-bin` / `--boot-bin`), then:

```bash
python flasher.py --iface "Ethernet 2"
```

The adapter name is shown in Windows "Network Connections". At startup the
size and CRC32 of both files are printed; check that every station shows the
same values. Stop with Ctrl+C.

If the bootloader stage fails, the console shows **DO NOT POWER OFF**. The
application is still running and the script retries; a board whose bootloader
stays broken must not be reset and needs SWD recovery.

## Multiple stations

Any number of PCs can run in parallel with the same settings. Each one is a
separate point-to-point link, so the IP addresses never clash. Every station
writes its own `results.csv` with its hostname (or `--station-id`) in the
`station` column. Collect the files and merge them:

```bash
python merge_results.py pc1.csv pc2.csv pc3.csv -o merged_results.csv
```

This prints the totals, the boards that never passed, and the boards that were
flashed more than once.

## Tests

```bash
python -m unittest discover -s tests -v
```

The tests run the full flow against a simulated board (`tests/fake_device.py`)
on 127.0.0.1, including retried transfers and lost commands.
