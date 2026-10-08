# MUEB4 flashing station

Flashes the application and bootloader firmware to MUEB4 boards over Ethernet,
fully automated, one board after another. The PC and the board are connected
directly with one network cable (no switch).

For every board:

1. Link up, the built-in DHCP server gives the board `10.6.18.5`
2. `kPing` checks that the application is running
3. `kStartFirmwareUpdate`, the board restarts into the bootloader
4. `mueb4-firmware-app.bin` is sent over TCP (port 50002); the board verifies it and answers `FLASH_OK` and restarts
5. `kGetFirmwareChecksum` is compared with the CRC32 of the file
6. `kStartBootloaderFirmwareUpdate`, then `mueb4-firmware-boot.bin` is sent; the board verifies it and answers `FLASH_OK`
7. `kGetBootloaderFirmwareChecksum` is compared with the CRC32 of the file
8. PASS/FAIL banner, beep, and a row in `results.csv`. Unplug the board, plug in the next one.

### TCP transfer

The image is padded with `0xFF` to a multiple of 4 bytes. Before the image, an
8-byte header is sent: image size and CRC32 (`zlib.crc32` of the padded image),
both big endian. The board rejects an invalid header before erasing anything.
After the host closes its side, the board computes the CRC32 of the flash
content with the hardware CRC unit and answers `FLASH_OK`, or `FLASH_ERROR` if
the size or the CRC32 doesn't match. The station then retries.

- The bootloader programs the application's first word (initial stack
  pointer) only after successful verification. An interrupted or corrupt
  transfer leaves the application invalid, and the board stays in the
  bootloader.
- After erasing the bootloader, the application never leaves the updater. On
  an error it waits for the station to reconnect. The station retries without
  sending `kStartBootloaderFirmwareUpdate` again.

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
application keeps waiting in its bootloader updater and the script reconnects
and retries; a board whose bootloader stays broken must not be reset and needs
SWD recovery.

### Boards with the old bootloader

The old bootloader (before image verification) expects the application image
without the 8-byte header. For boards that still have it, add
`--legacy-bootloader`:

```bash
python flasher.py --iface "Ethernet 2" --legacy-bootloader
```

The application is then sent without the header. The bootloader is always
sent with the header, because the new application flashes it. After this run
the board has both new firmwares, and later runs need no flag.

Use the flag only for old boards:

- Without the flag, the old bootloader writes the header into flash. The
  application doesn't start, and the board stays in the old bootloader's
  updater (it keeps listening on port 50002). The station can't recover it
  yet, because it starts every board with a ping to the application. Use SWD
  for now.
- With the flag, the new bootloader rejects the application (`FLASH_ERROR`)
  before erasing anything. The board keeps its firmware; rerun it without the
  flag.

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
