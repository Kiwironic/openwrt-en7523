# OpenWrt on EN7523 + MT7916 AX3000 WiFi Router

Custom files to build a working OpenWrt image for an EN7523-based AX3000
router with MT7916 dual-band 802.11ax WiFi and MT7530 Ethernet switch.
Includes LuCI, persistent NAND flash, WiFi, and NPU Wi-Fi-RX offload.

EN7523 SoC support exists upstream but is spread accross several trees
(OpenWrt PR #20104, Sirherobrine23/airoha_kernel, thienanh95/EN7523_gpon,
merbanan/airoha_ml) and not yet consolidated. This repo is a
board-validated subset for this specific router.

## Hardware

| Component | Details |
|---|---|
| SoC | Airoha/Econet EN7523 (EN7529 variant) |
| CPU | 2x Cortex-A53 @ 800 MHz, 32-bit ARMv7 mode |
| RAM | 256 MiB DDR3L-1866 |
| Flash | 256 MiB SLC SPI-NAND (Micron MT29F2G01) |
| WiFi | MediaTek MT7916AN + MT7976DN, dual-band 2x2 802.11ax |
| Ethernet | MediaTek MT7530, 4 ports with internal PHYs |
| PCIe | 2 root ports, both populated with MediaTek WiFi endpoints |
| NPU | 4x RV32 cores (Wi-Fi-RX hardware offload) |
| Fiber | xPON connector (GPON; not in this port) |
| UART | 115200 8N1, 3.3V, CN3 header |
| Buttons | Reset, WPS |
| LEDs | Power, PON, LOS, Internet, WPS, LAN1-4, 2.4GHz, 5GHz |

## Status

| Subsystem | Status | Notes |
|---|---|---|
| Kernel boot | **Working** | Linux 6.18.x, persistent NAND + TFTP RAM-boot |
| UART | **Working** | ttyS0 at 115200 |
| Ethernet | **Working** | airoha_eth + mt7530 DSA, 3 LAN + 1 WAN |
| PCIe | **Working** | Both root ports enumerate and enable |
| WiFi 2.4GHz | **Working** | mt7915e, phy0, AP mode |
| WiFi 5GHz | **Working** | mt7915e, phy1, AP mode |
| SPI-NAND / MTD | **Working** | 12 partitions (mtd0-mtd11) |
| NPU | **Working** | 4 RV32 cores boot, driver loaded as module |
| GPIO LEDs | **Working** | Power, PON, LOS, Internet (WPS LED doesn't light - see Known Issues) |
| LAN port LEDs | **Working** | MT7530 PHY LED controller (link + activity blink) |
| WiFi LEDs | **Working** | netdev trigger on phy0-ap0/phy1-ap0, solid on when AP up |
| Reset button | **Working** | gpio-keys-polled, KEY_RESTART |
| WPS button | **Untested** | Registered in DTS, not verified on hardware |
| Persistent flash | **Working** | `mtd write` to NAND, FIT kernel + squashfs rootfs |
| LuCI | **Working** | uhttpd + LuCI on LAN |
| Package manager | **Working** | apk (apk-mbedtls) |
| xPON / Fiber | **Not in this port** | GPON work in-flight upstream |

## Key Technical Details

### 1. PCI Quirk: Root Port BAR 0

The EN7523 PCIe root ports (MediaTek `14c3:0810` and `14c3:0811`) advertise
a 64-bit prefetchable BAR 0 of 8 GiB. On 32-bit ARM this BAR cannot be
assigned, causing `pci_enable_resources()` to fail. This blocks MMIO
forwarding to the WiFi endpoints, and mt7915e fails with an MCU semphore
timeout.

Fix: A PCI quirk clears BAR 0's resource flags for these root ports.
See `patches/kernel/960-pci-quirk-en7523-rootport-clear-bar0.patch`.

### 2. Module Load Order

WiFi driver dependency chain, loaded in this order:

```
compat.ko → cfg80211.ko → mac80211.ko → mt76.ko → mt76-connac-lib.ko
→ i2c-core.ko → hwmon.ko → mt7915e.ko
```

`hwmon.ko` needs `i2c-core.ko` (for `i2c_verify_client`).
`mt7915e.ko` needs `hwmon.ko` (for `devm_hwmon_device_register_with_groups`).

### 3. EEPROM / WiFi Calibration

The MT7916 EEPROM (4096 bytes) contains per-unit TX power and antenna
calibration. The efuse read path fails on this board, so the EEPROM is
loaded from the `reservearea` NAND partition at offset `0x4c000` via an
nvmem cell in the DTS. This location is confirmed accross multiple
EN7523+MT7916 boards.

The DTS wiring: `reservearea` partition has an `eeprom@4c000` cell, the
MT7916 PCIe node references it via `nvmem-cells = <&mt7916_eeprom_factory>`.
No static fallback file is needed.

### 4. NPU Firmware

The NPU requires two firmware blobs:
- `npu_rv32.bin` - RV32 core firmware (60584 bytes)
- `npu_data.bin` - NPU data table (712 bytes)

The driver must be built as a module (`CONFIG_ECONET_NPU=m`). When
built-in, it probes before rootfs mount and cant find the firmware.
As a module it loads after rootfs mount and all 4 cores boot fine.

The NPU is only used for Wi-Fi-RX hardware offload. The driver
(`dt/econet-npu-driver/econet-npu.c`) wires up `WIFI_MAIL` and
`econet_npu_wifi_offload_set_pkt_buf_addr` - no Ethernet PPE/QDMA/NAT.

Firmware blobs are not redistributed here (no redistribution rights).
Extract them from your own device. See `docs/Firmware-Extraction.md`.

### 4a. WiFi Firmware Version

The MT7916 WM firmware shipped with mt76 has datecode `20240823`. A newer
version (`20260428`) is available from another vendor's firmware. Not
included here - extract it yourself if needed.

This device's EEPROM has no precal data (flag at offset `0x19a` is `0x00`).
mt76 supports precal via an nvmem cell but none is needed for this board.

### 5. Kernel Load Address

Kernel must be loaded at `0x80208000` (not `0x80200000`). The 0x8000 offset
is the ARM `TEXT_OFFSET`. Loading at the aligned base causes silent boot
failure.

### 6. WiFi LED GPIO Mux Fix

The mt76 driver's `mt7915_init_led_mux()` writes to `MT_LED_GPIO_MUX1`
(0x70005054) via PCIe MMIO, but this register is outside the PCIe BAR
mapping so the write silently fails. A hotplug script uses the debugfs
regidx/regval interface to set the mux corectly.

The DTS includes a `led` sub-node (`led-sources = <2>`, `led-active-low`)
so mt76 registers LED class devices with correct polarity. The `01_leds`
board script assigns netdev triggers (link+rx) on `phy0-ap0`/`phy1-ap0`.

### 7. ujail / ubus Incompatibility

The stock `wpad.init` uses ujail to sandbox hostapd. On EN7523 the mount
namespace isolation breaks ubus socket connections. The patched
`wpad.init` in `package-fixes/` disables ujail,

## Build

See **[docs/BUILD.md](docs/BUILD.md)** for detailed instructions.

Quick summary:

```bash
git clone https://git.openwrt.org/openwrt/openwrt.git
cd openwrt
./scripts/feeds update -a
./scripts/feeds install luci luci-base luci-mod-admin-full \
  luci-theme-bootstrap luci-proto-ipv6 luci-proto-ppp \
  luci-app-firewall luci-app-wireguard

# Copy custom files from this repo (see docs/BUILD.md for paths)
make defconfig
make target/linux/clean
make -j$(nproc)
```

Images land in `bin/targets/airoha/en7523/`.

Critical config:
- `CONFIG_ARCH_AIROHA=y` - required for ARM GIC, without it the kernel panics
- `CONFIG_ECONET_NPU=m` - NPU driver must be a module, not built-in
- Kernel load address: `0x80208000`

## Flashing

> **Use `mtd -r write` to flash. Do NOT use `sysupgrade`** - it doesn't
> properly write the squashfs rootfs on this device.

1. Boot OpenWrt via TFTP initramfs (see `docs/TFTP-Transfer.md`)
2. Transfer the sysupgrade image:
   ```bash
   scp openwrt-airoha-en7523-*-sysupgrade.bin root@192.168.1.1:/tmp/
   ```
3. Flash:
   ```bash
   ssh root@192.168.1.1
   mtd -r write /tmp/openwrt-airoha-en7523-*-sysupgrade.bin firmware
   ```

The `firmware` partition (50 MiB at offset 0xc0000) replaces the vendor's
`tclinux` + `ubifs` partitions.

## Recovery

If bricked:

1. Connect via UART to the U-Boot prompt
2. Use chunked TFTP to load an initramfs image into RAM:
   - Split the image into 32 KiB chunks
   - Use a lockstep RFC-1350 TFTP server (512-byte blocks, no OACK)
   - Load each chunk: `tftpboot 0x8a000000+i*0x8000 chunkNNN`
   - Verify: `iminfo 0x8a000000` (must show `crc32+ sha1+`)
   - Boot: `bootm 0x8a000000`
3. Flash the sysupgrade image as above

See `docs/TFTP-Transfer.md` for the full procedure.

## Base-files

Board-specific configuration installed into the root filesystem:

- **`etc/board.d/01_leds`** - LED triggers: Power (default on), Internet
  (netdev on eth4), WiFi 2.4GHz/5GHz (netdev link+rx on phy0-ap0/phy1-ap0,
  50ms interval), WPS (default off)
- **`etc/board.d/02_network`** - LAN: eth1-3 (LAN4/LAN3/LAN2), WAN: eth4
  (LAN1), eth0 is DSA conduit
- **`etc/hotplug.d/ieee80211/15-wifi-led-mux`** - sets MT_LED_GPIO_MUX1
  via debugfs, reloads LED config after PHY registration
- **`etc/hotplug.d/ieee80211/20-enable-wifi`** - fallback WiFi enable
- **`etc/uci-defaults/99-enable-wifi`** - enable WiFi on first boot
- **`etc/uci-defaults/99-set-wifi-led-interval`** - 50ms blink interval
  for WiFi netdev LEDs
- **`etc/uci-defaults/99-fix-acl-perms`** - fix ACL file permissions
  (NTFS build host issue)
- **`lib/upgrade/platform.sh`** - sysupgrade platform handler

## Known Issues

### WPS LED Does Not Light

The WPS LED (GPIO 14) is configured in the DTS but doesnt light on this
hardware. Likely a conflict with SPI quad mode on the NAND flash. The LED
entry is kept in config but expected to stay off.

### NPU Driver Must Be a Module

`CONFIG_ECONET_NPU` must be `=m`, not `=y`. When built-in the driver probes
before rootfs mount and can't find the firmware files (fails with -110).
As a module it loads after rootfs mount and all 4 cores boot.

### xPON / GPON

The fiber port is not included. GPON support for EN7523 is in-flight
upstream - [thienanh95/EN7523_gpon](https://github.com/thienanh95/EN7523_gpon)
reports working GPON against some OLTs. The vendor driver targets Linux
4.4.x; porting to 6.18.x is a significant effort.

## Project Structure

```
openwrt-en7523-public/
├── README.md
├── .gitignore
├── target-makefile              target/linux/airoha/Makefile
├── Identification/             Board/chip/antenna photos
├── docs/
│   ├── BUILD.md
│   ├── Firmware-Extraction.md
│   ├── PCI-Quirk-EN7523.md
│   └── TFTP-Transfer.md
├── dt/
│   ├── en7523-ax3000-router.dts
│   └── econet-npu-driver/
│       ├── econet-npu.c
│       ├── Kconfig
│       └── Makefile
├── firmware/                   (gitignored - extract from your device)
├── patches/kernel/             Kernel + mt76 patches
│   ├── 001-fix-mt76-led-cflags.patch
│   ├── 002-fix-led-blink-threshold.patch
│   ├── 203-*.patch             Pinctrl fixes
│   ├── 900/901-*.patch         BMT support
│   ├── 913-*.patch             PCIe HB reset
│   ├── 915-*.patch             Flowtable offload + HW QoS
│   ├── 916-*.patch             HW GRO TCP
│   ├── 920-*.patch             Ethernet driver fixes
│   ├── 924-*.patch             NPU coherent mailbox DMA
│   ├── 930-*.patch             EN7523 Ethernet/DSA/PCS
│   ├── 950-*.patch             Econet NPU driver
│   └── 960-*.patch             PCI quirk for root port BAR 0
├── base-files/etc/
│   ├── board.d/                01_leds, 02_network
│   ├── hotplug.d/ieee80211/    15-wifi-led-mux, 20-enable-wifi
│   ├── uci-defaults/           99-enable-wifi, 99-fix-acl-perms, 99-set-wifi-led-interval
│   └── lib/upgrade/            platform.sh
├── image/
│   ├── Makefile
│   ├── en7523.mk
│   └── acl-perms.pseudo
├── package-fixes/
│   └── wpad.init
└── tooling/
    ├── tftp_lockstep.py
    └── load_and_boot.py
```

## UART

- 115200 8N1, no flow control
- 3.3V logic, CN3 header (VCC, TX, RX, GND)
- Don't connect VCC if the router is independently powered

## License

- DTS and custom driver code: GPL-2.0-only OR BSD-2-Clause
- Patches: same license as the files they modify
- Firmware blobs (`npu_rv32.bin`, `npu_data.bin`, `mt7916_eeprom.bin`):
  proprietary, no redistribution rights. Not included in this repo.
  Extract from your own device (see `docs/Firmware-Extraction.md`).

## Acknowledgments

This port builds on prior community work:

- **[openwrt/openwrt#20104](https://github.com/openwrt/openwrt/pull/20104)**
  by Sirherobrine23 - EN7523 Ethernet/QDMA/PPE + MT7530 DSA. The 930-42
  through 930-52 patches are a subset of this PR.
- **[Sirherobrine23/airoha_kernel](https://github.com/Sirherobrine23/airoha_kernel)**
  - NPU driver and related patches.
- **[merbanan/airoha_ml](https://github.com/merbanan/airoha_ml)** - Frame
  Engine tools and register docs. Provided the reference DTS for the
  reservearea nvmem cell and airoha,eth phandle.
- **[thienanh95/EN7523_gpon](https://github.com/thienanh95/EN7523_gpon)**
  - Ethernet/DSA patch subset originates from this tree.
- mt76 driver maintainers, the OpenWrt community, and Airoha/Econet.
- Community members who confirmed the EEPROM location across multiple
  EN7523+MT7916 boards and noted the newer WM firmware and precal patch.

Patch authors: Benjamin Larsson, Christian Marangi, Daniel Pawlik,
Daniel Schwierzeck, Lorenzo Bianconi, Matheus Sampaio Queiroga,
Robert Marko, Sayantan Nandy.
