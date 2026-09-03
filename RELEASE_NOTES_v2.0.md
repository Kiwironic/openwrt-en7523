# Release v2.0 — Documentation, Legal & Firmware Corrections

Addresses reviewer feedback from **merbanan** on the initial v1.0 release.
No build-affecting files (patches, DTS, base-files, driver) were changed in
this release — the corrections are documentation, legal, and firmware
handling.

## Reviewer Feedback

| # | Feedback point | Action |
|---|---|---|
| 1 | PCIe BAR quirk confirmed | No change needed (already in README §1 and `docs/PCI-Quirk-EN7523.md`) |
| 2 | Generic WiFi calibration is wrong; extract from reserved partition | Removed generic `mt7916_eeprom.bin` from repo. Documented extraction from device. |
| 3 | GPON is in the works, working against some OLTs | Softened "Not supported" to "Not in this port, in-flight upstream." Added reference to `thienanh95/EN7523_gpon`. |
| 4 | WiFi/NPU firmware have no redistribution rights | Removed `npu_rv32.bin`, `npu_data.bin`, `mt7916_eeprom.bin` from repo. Added `docs/Firmware-Extraction.md`. Updated License and `.gitignore`. |
| 5 | EN7523 support is scattered, not consolidated | Added scope note to README intro. Acknowledgments already cited all source trees. |
| 6 | NPU is only for Wi-Fi-RX hw-offload | Corrected NPU description throughout README. |

## Changes

### Removed
- `firmware/npu_rv32.bin` (60584 bytes) — NPU RV32 core firmware
- `firmware/npu_data.bin` (712 bytes) — NPU data table
- `firmware/mediatek/mt7916_eeprom.bin` (4096 bytes) — MT7916 WiFi EEPROM

### Added
- `docs/Firmware-Extraction.md` — extraction guide for firmware blobs

### Modified
- `README.md` — NPU description, GPON wording, EEPROM section, License, scope note
- `docs/BUILD.md` — firmware copy steps replaced with extraction instructions
- `.gitignore` — added `firmware/*.bin` and `local/`

## Acknowledgments

Thanks to **merbanan** for the detailed review.
