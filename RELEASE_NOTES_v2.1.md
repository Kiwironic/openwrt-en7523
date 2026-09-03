# Release v2.1 — DTS, LED, and Build Fixes

This release fixes several build-breaking and hardware-level issues discovered
during testing and community review. All changes are verified on hardware.

## Summary of Changes

### Build-breaking fixes

- **Added `image/en7523.mk`** — This file was missing from the public repo.
  Without it, the build system cannot define the device and no image is
  produced. The device is now defined as `airoha_en7523-ax3000-router`
  (previously used a vendor model name).
- **Renamed DTS** to `en7523-ax3000-router.dts` to match
  the `compatible` string and device definition.
- **Updated all board scripts** (`01_leds`, `02_network`, `20-enable-wifi`,
  `99-enable-wifi`, `platform.sh`) to use `airoha,en7523-ax3000-router` as
  the board name. The previous release changed the DTS `compatible` string
  but not the board scripts, causing the network config to fall through to
  "Unsupported hardware" and leaving the device unreachable after flash.

### WiFi LED fixes

- **Changed WiFi LED trigger** from `phy0tpt`/`phy1tpt` to `netdev` on
  `phy0-ap0`/`phy1-ap0` with `link rx` mode and 50ms interval. This gives
  the desired behavior: solid on when the AP is up, flashing on received
  traffic. The `phy0tpt` trigger kept the LEDs off at idle (no traffic).
- **Added `99-set-wifi-led-interval` uci-defaults script** to set the 50ms
  blink interval (not supported by `ucidef_set_led_netdev`).
- **Updated `15-wifi-led-mux` hotplug script** to reload the LED config
  via `/etc/init.d/led restart` instead of hardcoding `phy0tpt`/`phy1tpt`
  triggers (which would override the netdev trigger from uci).
- **DTS `led` sub-node** (`led-sources = <2>`, `led-active-low`) in the
  MT7916 device node — tells the mt76 driver to register LED class devices
  with correct polarity.

### EEPROM / WiFi Calibration

- **DTS nvmem cell** in `reservearea` partition at offset `0x4c000` — loads
  the EEPROM directly from flash at boot. Verified working: no "eeprom load
  fail" message, MAC addresses match device calibration data.
- **Updated `docs/Firmware-Extraction.md`** — corrected the EEPROM source
  from `ptdata` (UBI volume) to `reservearea` (raw flash at 0x4c000), as
  confirmed by merbanan and verified on hardware.
- **Updated README §3** — documents the nvmem cell approach and the
  `reservearea` offset.

### DTS additions

- **`airoha,npu = <&npu>` and `airoha,eth = <&eth>`** phandles in the
  MT7916 device node, matching the reference DTS from merbanan.

### Documentation

- **Added WPS LED to Known Issues** — GPIO 14 does not light on this
  hardware (likely SPI quad mode conflict).
- **Updated BUILD.md** — added `en7523.mk` copy step.
- **Updated README** — LED config section, status table, project structure.

## Reviewer Feedback — merbanan's points

| # | Feedback point | Status |
|---|---|---|
| 1 | PCIe BAR quirk confirmed | Already implemented (patch 960) |
| 2 | Don't use generic WiFi calibration; extract from art/reserved partition | Implemented: nvmem cell in `reservearea` at 0x4c000, static blob removed from repo |
| 3 | GPON is in the works, working against some OLTs | Documented in README status table and Known Issues |
| 4 | WiFi firmware and NPU firmware do not have redistribution rights | Firmware blobs removed, `.gitignore` prevents commits, extraction guide provided |
| 5 | EN7523 support exists but is scattered, not consolidated | Documented in README scope note and Acknowledgments |
| 6 | NPU is only used for Wi-Fi-RX hw-offload | Documented correctly throughout README |
| 7 | DTS: EEPROM in reservearea at 0x4c000, airoha,eth phandle, nvmem-cells | Implemented in DTS, verified on hardware |

## Reviewer Feedback — longnt2007's points

| # | Feedback point | Status |
|---|---|---|
| 1 | EEPROM in reservearea at 0x4c000 (confirmed on multiple EN7523+MT7916 boards) | Already implemented — nvmem cell at 0x4c000, verified on hardware |
| 2 | airoha,eth phandle in MT7916 node | Implemented in DTS |
| 3 | precal patch for MT7916 (ID 0x7906) in mtk feeds | mt76 already supports precal via nvmem; this device's EEPROM has no precal data (flag at 0x19a is 0x00) |
| 4 | Newer WM firmware (20260428) from TP-Link xx530v v2 | Documented in README §4a — not included, user must extract |

## Verified on Hardware

All changes validated on a running EN7523 AX3000 router (persistent NAND install):

- **Board name**: `airoha,en7523-ax3000-router`
- **WiFi 2.4GHz**: AP mode, SSID `OpenWrt-EN7523`, channel 1, HE20, WPA2-PSK
- **WiFi 5GHz**: AP mode, SSID `OpenWrt-EN7523-5G`, channel 36, HE80, WPA2-PSK
- **WiFi LEDs**: Both solid on (brightness 255), netdev trigger on phy0-ap0/phy1-ap0
- **Power LED**: On
- **WPS LED**: Off (known issue — GPIO 14 conflict)
- **EEPROM**: Loaded from nvmem cell (reservearea at 0x4c000) — no fallback to static file
- **NPU**: Works in initramfs; fails on NAND boot (built-in driver probes before rootfs mount)
- **Network**: LAN (eth1=LAN4, eth2=LAN3, eth3=LAN2) + WAN (eth4=LAN1), DSA working
- **LuCI**: Accessible on the LAN (HTTP 200)
- **PCI quirk**: Both root ports BAR 0 cleared, WiFi endpoints probing correctly
- **MTD partitions**: 12 (9 in DTS + 3 auto-created from firmware)
- **WPS button**: Registered in DTS but not verified on hardware

## Files Changed

### Added
- `image/en7523.mk` — device definition for the build system
- `base-files/etc/uci-defaults/99-set-wifi-led-interval` — WiFi LED blink interval

### Modified
- `base-files/etc/board.d/01_leds` — WiFi LED trigger changed to netdev (link+rx)
- `base-files/etc/hotplug.d/ieee80211/15-wifi-led-mux` — reload LED config instead of hardcoding triggers
- `dt/en7523-ax3000-router.dts` — added `led` sub-node, `airoha,npu`/`airoha,eth` phandles, nvmem cell in reservearea
- `docs/BUILD.md` — added en7523.mk copy step
- `docs/Firmware-Extraction.md` — corrected EEPROM source to reservearea
- `README.md` — LED config, status table, EEPROM section, Known Issues (WPS LED), project structure
- `RELEASE_NOTES_v2.0.md` — no changes (historical)

## Acknowledgments

Thanks to **merbanan** for the DTS example (reservearea nvmem cell, airoha,eth
phandle) and for confirming the PCIe BAR quirk and NPU usage.
