# Release v2.1 — Build Fixes, WiFi LEDs, NPU Module, DTS Corrections

Fixes several build-breaking and hardware-level issues discovered during
testing and community review. All changes verified on hardware (persistent
NAND install).

## Build-breaking Fixes

- **Added `image/en7523.mk`** — was missing from the public repo. Without it
  no image can be built. Device defined as `airoha_en7523-ax3000-router`.
- **Renamed DTS** to `en7523-ax3000-router.dts` to match the `compatible`
  string and device definition.
- **Fixed board name in all scripts** (`01_leds`, `02_network`,
  `20-enable-wifi`, `99-enable-wifi`, `platform.sh`) — the DTS `compatible`
  was changed to `airoha,en7523-ax3000-router` but the scripts still checked
  the old name, causing network config to fall through to "Unsupported
  hardware" and leaving the device unreachable after flash.

## WiFi LED Fixes

- **Changed trigger** from `phy0tpt`/`phy1tpt` to `netdev` on `phy0-ap0`/
  `phy1-ap0` with `link rx` mode and 50ms interval. Solid on when AP is up,
  flashes on received traffic. The `phy0tpt` trigger kept LEDs off at idle.
- **Added `99-set-wifi-led-interval`** uci-defaults script (50ms blink
  interval, not supported by `ucidef_set_led_netdev`).
- **Updated `15-wifi-led-mux`** hotplug to reload LED config via
  `/etc/init.d/led restart` instead of hardcoding `phy0tpt`/`phy1tpt`.
- **Added DTS `led` sub-node** (`led-sources = <2>`, `led-active-low`) —
  tells mt76 to register LED class devices with correct polarity.

## NPU Module Fix

- **Changed `CONFIG_ECONET_NPU` from `=y` to `=m`** — when built-in, the
  driver probes before the squashfs rootfs is mounted and cannot find the
  firmware files (fails with `-110`). As a module, it loads after rootfs
  mount and all 4 RV32 cores boot successfully.
- Module is installed to `lib/modules/` and loaded via `etc/modules.d/`.

## EEPROM / WiFi Calibration

- **Added nvmem cell** in `reservearea` partition at offset `0x4c000` —
  loads EEPROM directly from flash at boot. Verified: no fallback to static
  file, MAC addresses match device calibration data.
- **Updated `docs/Firmware-Extraction.md`** — corrected EEPROM source from
  `ptdata` (UBI volume) to `reservearea` (raw flash at 0x4c000).

## DTS Additions

- **`airoha,npu = <&npu>` and `airoha,eth = <&eth>`** phandles in the
  MT7916 device node.
- **`read-only`** restored on `reservearea` partition.

## Documentation

- Added WPS LED to Known Issues (GPIO 14 does not light on this hardware).
- Added WiFi firmware version note (§4a): mt76 ships WM firmware datecode
  `20240823`; a newer `20260428` is available from another vendor's firmware.
- Added precal note: this device's EEPROM has no precal data (flag at 0x19a
  is 0x00); mt76 supports precal via nvmem but no cell is needed.
- Corrected status table: 12 MTD partitions (not 10), 3 LAN + 1 WAN port
  (not 4 LAN), WPS button untested, LAN port LEDs link-only.
- Corrected network port mapping: eth4=LAN1/WAN, eth3=LAN2, eth2=LAN3,
  eth1=LAN4.
- Corrected flashing IP to 192.168.1.1 (was 192.168.2.1).

## Community Feedback

| # | Feedback point | Status |
|---|---|---|
| 1 | EEPROM in reservearea at 0x4c000 (confirmed across multiple boards) | Implemented — nvmem cell, verified on hardware |
| 2 | airoha,eth phandle in MT7916 node | Implemented in DTS |
| 3 | Precal patch for MT7916 (ID 0x7906) | mt76 supports precal via nvmem; this device has no precal data |
| 4 | Newer WM firmware (20260428) available | Documented in README §4a — not included |

## Verified on Hardware

- **Board**: `airoha,en7523-ax3000-router`
- **WiFi**: 2.4GHz (HE20) + 5GHz (HE80), AP mode, WPA2-PSK
- **WiFi LEDs**: Both solid on, netdev trigger on phy0-ap0/phy1-ap0
- **NPU**: All 4 cores booted, firmware loaded (module)
- **EEPROM**: Loaded from nvmem cell (reservearea at 0x4c000)
- **Network**: LAN (eth1-3) + WAN (eth4), DSA working
- **LuCI**: Accessible on LAN (HTTP 200)
- **PCI**: Both root ports BAR 0 cleared, WiFi probing correctly

## Files Changed

### Added
- `image/en7523.mk`
- `base-files/etc/uci-defaults/99-set-wifi-led-interval`
- `patches/kernel/002-fix-led-blink-threshold.patch`

### Modified
- `base-files/etc/board.d/01_leds` — netdev trigger (link+rx)
- `base-files/etc/hotplug.d/ieee80211/15-wifi-led-mux` — reload LED config
- `dt/en7523-ax3000-router.dts` — led sub-node, phandles, nvmem cell, read-only
- `docs/BUILD.md` — en7523.mk copy step, 002 patch
- `docs/Firmware-Extraction.md` — EEPROM source corrected to reservearea
- `README.md` — status table, LED config, NPU, EEPROM, Known Issues

## Acknowledgments

Thanks to **merbanan** for the DTS example (reservearea nvmem cell,
airoha,eth phandle) and PCIe/NPU confirmation. Thanks to community members
who confirmed the EEPROM location across multiple EN7523+MT7916 boards.
