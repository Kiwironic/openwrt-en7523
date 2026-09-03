# Release v2.0 — Documentation & Legal Corrections

This release addresses reviewer feedback from **merbanan** on the initial
v1.0 release. No build-affecting files in this public repository (patches,
DTS, base-files, driver) were changed — the corrections are documentation,
legal, and firmware handling. A build bug was found and fixed in the local
build tree (see "Build Bug Fix" below).

## Reviewer Feedback — Points and Actions

| # | Feedback point | Valid? | Action taken |
|---|---|---|---|
| 1 | PCIe BAR quirk confirmed | Yes | No change needed (already documented in README §1 and `docs/PCI-Quirk-EN7523.md`) |
| 2 | Generic WiFi calibration is wrong; extract proper tables from art/reserved partition | Yes | Removed the generic `mt7916_eeprom.bin` from the repo. Documented the correct extraction procedure from the `ptdata` UBI partition (`RT30xxEEPROM.bin`, first 4096 bytes). See `docs/Firmware-Extraction.md` and README §3. |
| 3 | GPON is in the works, working against some OLTs | Yes | Softened "Not supported" to "Not in this port, in-flight upstream." Added reference to `thienanh95/EN7523_gpon` which reports working GPON against some OLTs. See README status table and Known Issues. |
| 4 | WiFi firmware and NPU firmware do not have explicit redistribution rights | Yes | Removed `npu_rv32.bin`, `npu_data.bin`, and `mt7916_eeprom.bin` from the repository. Added `docs/Firmware-Extraction.md` with extraction instructions. Fixed License section to explicitly state no redistribution rights are claimed. Updated `.gitignore` to prevent accidental commits. |
| 5 | EN7523 support exists but is scattered, not consolidated | Yes (informational) | Added scope note to README intro clarifying this is a board-validated subset, not a consolidation effort. The Acknowledgments section already cited all source trees. |
| 6 | NPU is only used for Wi-Fi-RX hw-offload | Yes | Corrected NPU description throughout README: intro, hardware overview, status table, §4 NPU Firmware, Included Packages, and Known Issues. The driver (`econet-npu.c`) only wires up `WIFI_MAIL` and `econet_npu_wifi_offload_set_pkt_buf_addr` — no Ethernet PPE/QDMA/NAT involvement. |

## Summary of Changes

### Removed
- `firmware/npu_rv32.bin` (60584 bytes) — NPU RV32 core firmware
- `firmware/npu_data.bin` (712 bytes) — NPU data table
- `firmware/mediatek/mt7916_eeprom.bin` (4096 bytes) — MT7916 WiFi EEPROM

### Added
- `docs/Firmware-Extraction.md` — step-by-step guide to extract firmware blobs
  from the device's `ptdata` and `ubifs_slave` NAND partitions using `ubi-reader`

### Modified
- `README.md` — NPU description corrected (Wi-Fi-RX offload), GPON wording
  softened, EEPROM section rewritten with ptdata extraction info, License
  section updated (no redistribution rights claimed), scope note added
- `docs/BUILD.md` — firmware copy steps replaced with extraction instructions
- `.gitignore` — added `firmware/*.bin` and `local/` to prevent accidental
  commits of firmware blobs or per-device credentials

### Not changed (no build-affecting changes in this repo)
- Kernel patches (`patches/kernel/`)
- Device tree (`dt/`)
- NPU driver (`dt/econet-npu-driver/`)
- Base-files (`base-files/`)
- Image build files (`image/`)
- Target Makefile (`target-makefile`)

### Build Bug Fix (local build tree, not in this repo)
During validation, a bug was found in the local T3 build tree: the `files/`
overlay directory contained a 208896-byte `mt7916_eeprom.bin` (the full
`RT30xxEEPROM.bin`) instead of the correct 4096-byte calibration file. The
mt76 driver could not load the oversized file and fell back to a built-in
default, meaning WiFi was running without per-unit calibration. This was
fixed by replacing the file in `files/lib/firmware/mediatek/` with the
correct 4096-byte EEPROM, rebuilding, and reflashing. The image was
verified to contain the correct 4096-byte EEPROM.

## Firmware Extraction

To build a working image, you must extract the three firmware blobs from your
own device. See **[docs/Firmware-Extraction.md](docs/Firmware-Extraction.md)**
for the full procedure. Quick summary:

1. Dump `ptdata` (mtd9) and `ubifs_slave` (mtd7) from the running device
2. Use `ubi-reader` to extract `RT30xxEEPROM.bin` from ptdata and `npu_rv32.bin`/
   `npu_data.bin` from ubifs_slave
3. Extract the first 4096 bytes of `RT30xxEEPROM.bin` as `mt7916_eeprom.bin`
4. Place all three in the build tree per `docs/BUILD.md`

## Verified on Hardware

All changes were validated on a running EN7523 AX3000 router:
- WiFi: both radios (2.4GHz + 5GHz) working in AP mode with WPA2-PSK
- Ethernet: LAN port 2 (eth3) verified with link; DSA working
- LEDs: Power, PON, Internet (netdev), WPS, WiFi 2.4GHz (phy0tpt), WiFi 5GHz
  (phy1tpt), LOS — all present with correct triggers
- LuCI: web UI accessible on port 80
- NPU: firmware present on device but probe times out (-110) — known issue,
  not critical (NPU is only for Wi-Fi-RX offload)
- WiFi EEPROM: the previous build had a bug where the 208896-byte
  `RT30xxEEPROM.bin` was placed as `mt7916_eeprom.bin` instead of the correct
  4096-byte file. The driver could not load it and fell back to a built-in
  default. This was fixed in the rebuild — the correct 4096-byte calibration
  extracted from ptdata is now in the image. The `eeprom load fail, use default
  bin` message is expected: it means the primary load path (DT nvmem cell /
  MTD / efuse) failed, and the driver falls back to loading
  `mediatek/mt7916_eeprom.bin` from `/lib/firmware/`. The long-term fix is to
  wire an nvmem cell from the ptdata partition in the DTS (see README §3).

## Acknowledgments

Thanks to **merbanan** for the detailed review and corrections.
