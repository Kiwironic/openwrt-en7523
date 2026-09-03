# Release v2.1 - Build Fixes, WiFi LEDs, NPU Module, DTS Corrections

Fixes build-breaking and hardware-level issues found during testing and
community review. All changes verified on hardware (persistent NAND install)

## Build-breaking Fixes

- **Added `image/en7523.mk`** - was missing from the repo, no image could
  be built without it. Device defined as `airoha_en7523-ax3000-router`.
- **Renamed DTS** to `en7523-ax3000-router.dts` to match the `compatible`
  string.
- **Fixed board name in all scripts** - the DTS `compatible` was changed
  to `airoha,en7523-ax3000-router` but `01_leds`, `02_network`,
  `20-enable-wifi`, `99-enable-wifi`, and `platform.sh` still checked the
  old name. This caused network config to fall through to "Unsupported
  hardware" and bricked the device after flash

## WiFi LED Fixes

- Changed trigger from `phy0tpt`/`phy1tpt` to `netdev` on `phy0-ap0`/
  `phy1-ap0` (link+rx, 50ms interval). Solid on when AP is up, flashes
  on received traffic. The old `phy0tpt` trigger kept LEDs off at idle.
- Added `99-set-wifi-led-interval` uci-defaults script (50ms interval
  not supported by `ucidef_set_led_netdev`).
- Updated `15-wifi-led-mux` hotplug to reload LED config via
  `/etc/init.d/led restart` instead of hardcoding triggers.
- Added DTS `led` sub-node (`led-sources = <2>`, `led-active-low`).

## NPU Module Fix

Changed `CONFIG_ECONET_NPU` from `=y` to `=m`. When built-in the driver
probes before rootfs mount and cant find the firmware (fails with -110).
As a module it loads after rootfs mount and all 4 RV32 cores boot.
Module installed to `lib/modules/` and loaded via `etc/modules.d/`.

## EEPROM / WiFi Calibration

Added nvmem cell in `reservearea` partition at offset `0x4c000`. Loads
EEPROM directly from flash at boot. Verified: no fallback to static file,
MAC addresses match device calibration. This location is confirmed accross
multiple EN7523+MT7916 boards.

Updated `docs/Firmware-Extraction.md` - EEPROM source corrected from
`ptdata` (UBI volume) to `reservearea` (raw flash at 0x4c000).

## DTS Additions

- `airoha,npu = <&npu>` and `airoha,eth = <&eth>` phandles in MT7916 node
- `read-only` restored on `reservearea` partition

## Documentation

- WPS LED added to Known Issues (GPIO 14 doesn't light on this hardware)
- WiFi firmware version note: mt76 ships WM firmware datecode `20240823`,
  newer `20260428` available from another vendor's firmware
- Precal note: this device's EEPROM has no precal data (flag at 0x19a is
  0x00), mt76 supports precal via nvmem but no cell needed
- Status table corrected: 12 MTD partitions, 3 LAN + 1 WAN, WPS button
  untested, network port mapping fixed
- Flashing IP corrected to 192.168.1.1

## Community Feedback

| # | Feedback | Status |
|---|---|---|
| 1 | EEPROM in reservearea at 0x4c000 (confirmed across multiple boards) | Implemented, verified on hardware |
| 2 | airoha,eth phandle in MT7916 node | Implemented in DTS |
| 3 | Precal patch for MT7916 (0x7906) | mt76 supports precal via nvmem; this device has no precal data |
| 4 | Newer WM firmware (20260428) available | Documented in README §4a, not included |

## Verified on Hardware

- Board: `airoha,en7523-ax3000-router`
- WiFi: 2.4GHz (HE20) + 5GHz (HE80), AP mode, WPA2-PSK
- WiFi LEDs: both solid on, netdev trigger
- NPU: 4 cores booted, firmware loaded (module)
- EEPROM: loaded from nvmem cell (reservearea at 0x4c000)
- Network: LAN (eth1-3) + WAN (eth4), DSA working
- LuCI: accessible on LAN
- PCI: both root ports BAR 0 cleared

## Files Changed

**Added:** `image/en7523.mk`, `base-files/etc/uci-defaults/99-set-wifi-led-interval`, `patches/kernel/002-fix-led-blink-threshold.patch`

**Modified:** `base-files/etc/board.d/01_leds`, `base-files/etc/hotplug.d/ieee80211/15-wifi-led-mux`, `dt/en7523-ax3000-router.dts`, `docs/BUILD.md`, `docs/Firmware-Extraction.md`, `README.md`

## Acknowledgments

Thanks to merbanan for the DTS example (reservearea nvmem cell,
airoha,eth phandle) and PCIe/NPU confirmation. Thanks to community members
who confirmed the EEPROM location across multiple EN7523+MT7916 boards.
