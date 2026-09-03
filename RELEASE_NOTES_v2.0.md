# Release v2.0 - Documentation & Firmware Corrections

Addresses reviewer feedback from merbanan on the initial v1.0 release.
No build-affecting files were changed - corrections are documentation,
legal, and firmware handling.

## Reviewer Feedback

| # | Feedback | Action |
|---|---|---|
| 1 | PCIe BAR quirk confirmed | No change needed (already in README and patch 960) |
| 2 | Generic WiFi calibration is wrong | Removed generic `mt7916_eeprom.bin` from repo, documented extraction |
| 3 | GPON is in the works, working against some OLTs | Softened "Not supported" to "Not in this port", added thienanh95/EN7523_gpon reference |
| 4 | No redistribution rights for firmware | Removed `npu_rv32.bin`, `npu_data.bin`, `mt7916_eeprom.bin`. Added `docs/Firmware-Extraction.md`. Updated License and `.gitignore` |
| 5 | EN7523 support is scattered | Added scope note to README intro |
| 6 | NPU is only for Wi-Fi-RX offload | Corrected NPU description throughout README |

## Changes

**Removed:** `firmware/npu_rv32.bin`, `firmware/npu_data.bin`, `firmware/mediatek/mt7916_eeprom.bin`

**Added:** `docs/Firmware-Extraction.md`

**Modified:** `README.md`, `docs/BUILD.md`, `.gitignore`

## Acknowledgments

Thanks to merbanan for the review.
