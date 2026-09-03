# OpenWrt on EN7523 + MT7916 AX3000 WiFi Router

This project contains all custom files needed to build a fully working OpenWrt
image for an EN7523-based AX3000 WiFi router with MT7916 dual-band 802.11ax WiFi
and MT7530 Ethernet switch. The image includes LuCI web UI, persistent NAND
flash (sysupgrade), WiFi, Wi-Fi-RX hardware offload via the NPU, and more.

> **Scope note:** EN7523 SoC support exists in the upstream community but is
> spread across several trees (OpenWrt PR #20104, Sirherobrine23/airoha_kernel,
> thienanh95/EN7523_gpon, merbanan/airoha_ml) and not yet consolidated. This
> repository is a **board-validated subset** for this specific router, not a
> consolidation effort. See the Acknowledgments section for the full provenance.

## Related Projects

- **[openwrt/openwrt#20104](https://github.com/openwrt/openwrt/pull/20104)** —
  Upstream draft PR for EN7523 Ethernet/QDMA/PPE + MT7530 DSA. This port is a
  validated subset of that work, adapted for this specific board.
- **[Sirherobrine23/airoha_kernel](https://github.com/Sirherobrine23/airoha_kernel)** —
  Reference kernel tree with EN7523 SoC support (NPU driver, Ethernet, xPON).
- **[thienanh95/EN7523_gpon](https://github.com/thienanh95/EN7523_gpon)** —
  EN7523 GPON OpenWrt tree, source of the Ethernet/DSA patch subset.
- **[merbanan/airoha_ml](https://github.com/merbanan/airoha_ml)** —
  Airoha Frame Engine tools and register documentation.

## Hardware Overview

| Component | Details |
|---|---|
| SoC | Airoha/Econet EN7523 (EN7529 variant) |
| CPU | 2x Cortex-A53 @ 800 MHz, running in 32-bit ARMv7 mode |
| RAM | 256 MiB DDR3L-1866 |
| Flash | 256 MiB SLC SPI-NAND (Micron MT29F2G01) |
| WiFi | MediaTek MT7916AN + MT7976DN dual-band 2x2 802.11ax |
| Ethernet Switch | MediaTek MT7530 (4x LAN, internal PHYs) |
| PCIe | 2 root ports (both populated with MediaTek WiFi endpoints) |
| NPU | 4x RV32 cores (EcoNet NPU — Wi-Fi-RX hardware offload) |
| Fiber | xPON connector (GPON; not in this port, in-flight upstream) |
| UART | 115200 8N1, 3.3V logic, CN3 header |
| Buttons | Reset, WPS |
| LEDs | Power, PON, LOS, Internet, WPS, LAN1-4, 2.4GHz, 5GHz |

## Current Status

| Subsystem | Status | Notes |
|---|---|---|
| Kernel boot | **Working** | Linux 6.18.x, persistent NAND + TFTP RAM-boot |
| UART console | **Working** | ttyS0 at 115200 |
| Ethernet | **Working** | airoha_eth + mt7530 DSA, 3 LAN + 1 WAN port |
| PCIe | **Working** | Both root ports enumerate and enable |
| WiFi 2.4GHz | **Working** | mt7915e driver, phy0, scan + AP mode |
| WiFi 5GHz | **Working** | mt7915e driver, phy1, scan + AP mode |
| SPI-NAND / MTD | **Working** | 12 partitions detected (mtd0-mtd11) |
| NPU | **Working** | All 4 RV32 cores boot, firmware loaded as module (not built-in) |
| GPIO LEDs | **Working** | 4 SoC GPIO LEDs: Power, PON, LOS, Internet (WPS GPIO 14 does not light — see Known Issues) |
| LAN port LEDs | **Working** | MT7530 PHY LED controller enabled (link + activity blink) |
| WiFi LEDs | **Working** | netdev trigger on phy0-ap0/phy1-ap0 (solid on when AP up, flash on rx) via GPIO mux fix + DTS led sub-node |
| Reset button | **Working** | gpio-keys-polled, KEY_RESTART |
| WPS button | **Untested** | gpio-keys-polled, KEY_WPS_BUTTON — registered in DTS but not verified on hardware |
| Persistent flash | **Complete** | `mtd write` to NAND (NOT sysupgrade), FIT kernel + squashfs rootfs |
| LuCI web UI | **Working** | uhttpd + LuCI, accessible on LAN |
| Package manager | **Working** | apk (apk-mbedtls) |
| xPON / Fiber | **Not in this port** | GPON work in-flight upstream (thienanh95/EN7523_gpon), working against some OLTs |

## Key Technical Findings

### 1. PCI Quirk: EN7523 Root Port BAR 0 (Critical for WiFi)

The EN7523 PCIe root ports (MediaTek `14c3:0810` and `14c3:0811`) advertise a
64-bit prefetchable BAR 0 of 8 GiB. On 32-bit ARM, this BAR cannot be assigned,
causing `pci_enable_resources()` to fail with `-EINVAL`. This prevents the
`pcieport` driver from enabling the bridge, which blocks MMIO forwarding to the
WiFi endpoints.

The WiFi driver (`mt7915e`) then fails with a 5-second MCU semaphore timeout
because MMIO writes never reach the radio MCU, followed by a NULL pointer
dereference in `mt76_txq_schedule_pending`.

**Fix:** A PCI quirk clears BAR 0's resource flags for these root ports,
allowing `pci_enable_resources()` to skip the unassignable BAR and successfully
enable the bridge. See `patches/kernel/960-pci-quirk-en7523-rootport-clear-bar0.patch`
and `docs/PCI-Quirk-EN7523.md` for full details.

### 2. Module Load Order

The WiFi driver has a dependency chain that must be loaded in this exact order:

```
compat.ko → cfg80211.ko → mac80211.ko → mt76.ko → mt76-connac-lib.ko
→ i2c-core.ko → hwmon.ko → mt7915e.ko
```

- `hwmon.ko` needs `i2c-core.ko` (for `i2c_verify_client`)
- `mt7915e.ko` needs `hwmon.ko` (for `devm_hwmon_device_register_with_groups`)

### 3. EEPROM / WiFi Calibration

The MT7916 EEPROM (4096 bytes) contains per-unit TX power and antenna
calibration data. The efuse read path fails on this board, so the EEPROM is
loaded from the `reservearea` NAND partition at offset `0x4c000` via an nvmem
cell in the DTS. This was confirmed by merbanan (who checked multiple EN7523 +
MT7916 boards) and verified on this device — the data at `reservearea+0x4c000`
starts with the device MAC address and matches the `RT30xxEEPROM.bin` UBI
volume on the `ptdata` partition.

The DTS includes the nvmem cell wiring (see `dt/en7523-ax3000-router.dts`):
the `reservearea` partition has an `eeprom@4c000` cell, and the MT7916 PCIe
device node references it via `nvmem-cells = <&mt7916_eeprom_factory>`. The
mt76 driver loads the EEPROM from flash at boot. A static fallback file
(`mt7916_eeprom.bin`) can be placed in the build tree as a safety net — see
`docs/Firmware-Extraction.md`.

### 4. NPU Firmware

The EN7523 NPU requires two firmware blobs:
- `npu_rv32.bin` — RV32 core firmware (60584 bytes)
- `npu_data.bin` — NPU data table (712 bytes)

Both are loaded at boot. The NPU driver is built as a module
(`CONFIG_ECONET_NPU=m`) so it loads after the rootfs is mounted, avoiding
a firmware-not-found error that occurs when built-in. The NPU is used for
**Wi-Fi-RX hardware offload** (not Ethernet PPE/QDMA or NAT offload). The
driver (`dt/econet-npu-driver/econet-npu.c`) only wires up the `WIFI_MAIL`
mailbox and `econet_npu_wifi_offload_set_pkt_buf_addr` path.

These firmware blobs are **not redistributed** in this repository — they lack
explicit redistribution rights. Extract them from your own device. See
`docs/Firmware-Extraction.md`.

### 4a. WiFi Firmware Version

The MT7916 WM firmware shipped with mt76 has build datecode `20240823`. A
newer version (`20260428`) is available from another vendor's firmware
image. The newer firmware is not included here — extract it from that
vendor's firmware if needed and place it in `lib/firmware/mediatek/`.

This device's EEPROM does not contain precal data (the `MT_EE_DO_PRE_CAL_V2`
flag at offset `0x19a` is `0x00`). The mt76 driver supports precal loading
via an nvmem `precal` cell, but no precal cell is needed for this board.

### 5. Kernel Load Address

The kernel must be loaded at `0x80208000` (not `0x80200000`). The 0x8000 offset
is the ARM `TEXT_OFFSET`. Loading at the aligned base causes a silent boot
failure because `PHYS_OFFSET` is computed incorrectly.

### 6. WiFi LED GPIO Mux Fix

The mt76 driver's `mt7915_init_led_mux()` writes to `MT_LED_GPIO_MUX1`
(0x70005054) via the PCIe MMIO bus, but this register is outside the PCIe BAR
mapping so the write silently fails. A hotplug script
(`base-files/etc/hotplug.d/ieee80211/15-wifi-led-mux`) uses the debugfs
regidx/regval interface to set the mux correctly, routing GPIO 14/15 on the
MT7916 to the LED controller.

The DTS also includes a `led` sub-node in the MT7916 device node with
`led-sources = <2>` and `led-active-low`, which tells the mt76 driver to
register LED class devices and set the correct polarity. The `01_leds`
board script assigns `netdev` triggers (link+rx mode) on `phy0-ap0`/`phy1-ap0`
to these LEDs, and the hotplug script reloads the LED config after the PHY
is registered.

### 7. ujail / ubus Incompatibility

The stock `wpad.init` uses ujail to sandbox hostapd and wpa_supplicant. On
EN7523, the mount namespace isolation prevents ubus socket connections from
working. The patched `wpad.init` in `package-fixes/` disables ujail while
keeping everything else intact.

## Build Instructions

For detailed step-by-step build instructions, see **[docs/BUILD.md](docs/BUILD.md)**.

Quick summary:

```bash
git clone https://git.openwrt.org/openwrt/openwrt.git
cd openwrt
./scripts/feeds update -a
./scripts/feeds install luci luci-base luci-mod-admin-full \
  luci-theme-bootstrap luci-proto-ipv6 luci-proto-ppp \
  luci-app-firewall luci-app-wireguard

# Copy custom files from this repo (see docs/BUILD.md for exact paths)
# ... copy patches, DTS, driver, firmware, base-files, image files ...

make defconfig
make target/linux/clean
make -j$(nproc)
```

The initramfs image will be at:
```
bin/targets/airoha/en7523/openwrt-airoha-en7523-*-initramfs-kernel.bin
```

The sysupgrade image will be at:
```
bin/targets/airoha/en7523/openwrt-airoha-en7523-*-sysupgrade.bin
```

### Critical Build Config

- `CONFIG_ARCH_AIROHA=y` — **Required** to enable ARM GIC/GICv3. Without it,
  the kernel panics immediately.
- Kernel load/entry: `0x80208000`
- Target: Airoha EN7523, Subtarget: EN7523

### Included Packages

The image includes:
- **LuCI** web UI (luci, luci-base, luci-mod-admin-full, luci-theme-bootstrap)
- **uhttpd** HTTP server for LuCI
- **apk-mbedtls** package manager
- **WireGuard** VPN (luci-app-wireguard, wireguard-tools, kmod-wireguard)
- **tcpdump** packet analyzer
- **mt76** / **mt7915e** WiFi driver
- **NPU** Wi-Fi-RX hardware offload (econet-npu driver + firmware)

## Flashing to NAND

Once the sysupgrade image is built, it can be flashed to the router's NAND
flash for persistent installation:

> **IMPORTANT: Use `mtd -r write /tmp/sysupgrade.bin firmware` to flash. Do NOT
> use `sysupgrade` on this device — it does not properly write the squashfs
> rootfs. Factory reset (5s reset button) would expose the old squashfs without
> LuCI. `mtd write` overwrites the entire firmware partition (kernel +
> squashfs).**

1. Boot OpenWrt via TFTP initramfs (see Recovery below or `docs/TFTP-Transfer.md`)
2. Transfer the sysupgrade image to the router:
   ```bash
   scp openwrt-airoha-en7523-*-sysupgrade.bin root@192.168.1.1:/tmp/
   ```
3. Flash with `mtd` (NOT `sysupgrade`):
   ```bash
   ssh root@192.168.1.1
   mtd -r write /tmp/openwrt-airoha-en7523-*-sysupgrade.bin firmware
   ```
4. The router will write the FIT kernel + squashfs rootfs to the `firmware`
   NAND partition and reboot into the persistent installation.

The `platform.sh` upgrade script uses `fit_do_upgrade` to write the FIT image
to the NAND partition. The `firmware` partition (50 MiB at offset 0xc0000)
replaces the vendor's `tclinux` + `ubifs` partitions.

## Recovery

If the router is bricked or you need to reflash from scratch:

1. Connect via UART to the U-Boot prompt
2. Use the chunked TFTP method to load an initramfs image into RAM:
   - Split the initramfs image into 32 KiB chunks
   - Use a lockstep RFC-1350 TFTP server (512-byte blocks, no OACK)
   - Load each chunk sequentially: `tftpboot 0x8a000000+i*0x8000 chunkNNN`
   - Verify with `iminfo 0x8a000000` (must show `crc32+ sha1+`)
   - Boot: `bootm 0x8a000000`
3. Once OpenWrt is running from RAM, flash the sysupgrade image as above

See `docs/TFTP-Transfer.md` for the full chunked transfer procedure.

## Base-files Configuration

The `base-files/` directory contains board-specific configuration that is
installed into the image's root filesystem:

### LED Configuration (`etc/board.d/01_leds`)

Defines LED triggers for the board:
- Power LED: default on
- Internet LED: netdev trigger on `eth4` (link/tx/rx)
- WiFi 2.4GHz LED: netdev trigger on `phy0-ap0` (link/rx, 50ms interval) — solid on when AP is up, flashes on received traffic
- WiFi 5GHz LED: netdev trigger on `phy1-ap0` (link/rx, 50ms interval) — solid on when AP is up, flashes on received traffic
- WPS LED: default off (does not light on this hardware — see Known Issues)

### Network Configuration (`etc/board.d/02_network`)

Sets up LAN/WAN interfaces via DSA:
- LAN: `eth1 eth2 eth3` (physical LAN4, LAN3, LAN2)
- WAN: `eth4` (physical LAN1, repurposed as WAN in router mode)
- `eth0` is the DSA conduit (not in the bridge)

### WiFi LED Mux Fix (`etc/hotplug.d/ieee80211/15-wifi-led-mux`)

Runs when `phy0` is added. Uses the mt76 debugfs interface to set
`MT_LED_GPIO_MUX1` (0x70005054) to `0x33000000`, routing GPIO 14/15 on the
MT7916 to the LED controller. Without this, WiFi LEDs never light up.

### WiFi Enable (`etc/hotplug.d/ieee80211/20-enable-wifi`)

Fallback script that enables WiFi radios when the `wireless` config is
generated by `10-wifi-detect`. Ensures WiFi comes up even if the uci-defaults
script runs before the config exists.

### WiFi Enable (`etc/uci-defaults/99-enable-wifi`)

Runs once on first boot to enable WiFi radios (`disabled=0`).

### WiFi LED Blink Interval (`etc/uci-defaults/99-set-wifi-led-interval`)

Sets the 50ms blink interval for the WiFi netdev LEDs. The
`ucidef_set_led_netdev` function in `board.d/01_leds` does not support the
`interval` option, so this script adds it after the board config is generated.

### ACL Permissions Fix (`etc/uci-defaults/99-fix-acl-perms`)

Fixes ACL/capabilities JSON file permissions for initramfs boots on NTFS build
hosts. NTFS doesn't support Unix permissions, so all files are 0755; ubusd
rejects ACL files that are not 0644. For sysupgrade images, this is handled at
build time via `image/acl-perms.pseudo`.

### Platform Upgrade (`lib/upgrade/platform.sh`)

Defines the sysupgrade path using `fit_check_image` for validation and
`fit_do_upgrade` for writing to NAND.

## Known Issues

### WPS LED Does Not Light

The WPS LED (GPIO 14) is configured in the DTS and registered as
`green:wps`, but it does not light on this hardware. GPIO 14 may conflict
with SPI quad mode on the NAND flash, preventing the GPIO from being
claimed. The LED entry is kept in the config for completeness but is
expected to stay off.

### NPU Driver Must Be a Module

The NPU driver (`econet-npu`) must be built as a module (`CONFIG_ECONET_NPU=m`),
not built-in. When built-in, it probes before the squashfs rootfs is mounted
and cannot find the firmware files, failing with `-110` (timeout). As a
module, it loads after rootfs mount and all 4 cores boot successfully.

### xPON / GPON

The fiber/xPON port is **not included in this port**. GPON support for EN7523
is in-flight upstream — the [thienanh95/EN7523_gpon](https://github.com/thienanh95/EN7523_gpon)
tree reports working GPON against some OLTs. The vendor driver stack targets
the stock kernel (Linux 4.4.x); porting to 6.18.x is a significant effort
being pursued by the community. This repository focuses on the Ethernet/WiFi
router use case.

## Project Structure

```
openwrt-en7523-public/
├── README.md                    This file
├── .gitignore
├── target-makefile              Airoha target Makefile (target/linux/airoha/Makefile)
├── Identification/              Board/chip/antenna photos for hardware reference
├── docs/
│   ├── BUILD.md                 Detailed build instructions
│   ├── Firmware-Extraction.md   How to extract firmware blobs from your device
│   ├── PCI-Quirk-EN7523.md      PCI BAR 0 quirk explanation
│   └── TFTP-Transfer.md         Chunked TFTP RAM-boot procedure
├── dt/
│   ├── en7523-ax3000-router.dts Device tree for the EN7523 AX3000 router
│   └── econet-npu-driver/       EcoNet NPU kernel driver (out-of-tree)
│       ├── econet-npu.c
│       ├── Kconfig
│       └── Makefile
├── firmware/                   (gitignored — extract from your device)
│   ├── npu_data.bin             NPU data table firmware
│   ├── npu_rv32.bin             NPU RV32 core firmware
│   └── mediatek/
│       └── mt7916_eeprom.bin    MT7916 calibration EEPROM
├── patches/
│   └── kernel/                  Custom kernel + mt76 patches
│       ├── 001-fix-mt76-led-cflags.patch
│       ├── 002-fix-led-blink-threshold.patch
│       ├── 203-*.patch          Pinctrl fixes
│       ├── 900-*.patch          BMT support
│       ├── 901-*.patch          SNAND BMT support
│       ├── 913-*.patch          PCIe HB reset
│       ├── 915-*.patch          Flowtable offload + HW QoS
│       ├── 916-*.patch          HW GRO TCP
│       ├── 920-*.patch          Ethernet driver fixes
│       ├── 924-*.patch          NPU coherent mailbox DMA
│       ├── 930-*.patch          EN7523 Ethernet/DSA/PCS patch set
│       ├── 950-*.patch          Econet NPU driver
│       └── 960-*.patch          PCI quirk for root port BAR 0
├── base-files/                  Board-specific root filesystem files
│   └── etc/
│       ├── board.d/
│       │   ├── 01_leds          LED trigger configuration
│       │   └── 02_network       LAN/WAN interface setup
│       ├── hotplug.d/ieee80211/
│       │   ├── 15-wifi-led-mux  WiFi LED GPIO mux fix
│       │   └── 20-enable-wifi   WiFi enable fallback
│       ├── uci-defaults/
│       │   ├── 99-enable-wifi   Enable WiFi on first boot
│       │   ├── 99-fix-acl-perms Fix ACL file permissions
│       │   └── 99-set-wifi-led-interval  WiFi LED blink interval
│       └── lib/upgrade/
│           └── platform.sh      Sysupgrade platform handler
├── image/                       Image build files
│   ├── Makefile                 target/linux/airoha/image/Makefile
│   ├── en7523.mk                target/linux/airoha/image/en7523.mk (device definitions)
│   └── acl-perms.pseudo         Squashfs permission overrides
├── package-fixes/
│   └── wpad.init                Patched wpad init script (ujail disabled)
└── tooling/
    ├── tftp_lockstep.py         Lockstep RFC-1350 TFTP server for chunked loading
    └── load_and_boot.py         Automated U-Boot serial + TFTP boot script
```

## UART Connection

- Baud: 115200, 8N1, no flow control
- Voltage: 3.3V logic
- Header: CN3 (4-pin: VCC, TX, RX, GND)
- Do NOT connect VCC if the router is independently powered

## License

- DTS and custom driver code: GPL-2.0-only OR BSD-2-Clause (matching OpenWrt
  conventions)
- Patches: Same license as the files they modify
- **Firmware blobs** (`npu_rv32.bin`, `npu_data.bin`, `mt7916_eeprom.bin`):
  Proprietary. **No redistribution rights are claimed or granted.** These
  blobs are not included in this repository. You must extract them from your
  own device. See `docs/Firmware-Extraction.md`.

## Acknowledgments

This port builds on substantial prior community work. The Ethernet/DSA
foundation, NPU driver, and many patches originate from or were inspired by
the following sources:

### Primary reference — OpenWrt PR #20104

- **[openwrt/openwrt#20104](https://github.com/openwrt/openwrt/pull/20104)** —
  "airoha: en7523: Add many devices and enable drivers" by
  [@Sirherobrine23](https://github.com/Sirherobrine23) (draft, unmerged).
  The EN7523 gen-2 Ethernet/QDMA, MT7530 DSA switch, and EN7523 PCS patches
  (930-42 through 930-52) are a subset of this PR, selected and validated
  against this specific board.

### Reference repositories

- **[Sirherobrine23/airoha_kernel](https://github.com/Sirherobrine23/airoha_kernel)** —
  Linux kernel with EN7523 SoC patches from OpenWrt. Source of the NPU
  driver (`econet-npu.c`) and related patches.
- **[merbanan/airoha_ml](https://github.com/merbanan/airoha_ml)** —
  Airoha SoC machine-learning and Frame Engine tools. Used for register-level
  reverse engineering of the QDMA/PPE blocks. Also provided the reference DTS
  for the MT7916 nvmem cell in `reservearea` and the `airoha,eth` phandle.
- **[thienanh95/EN7523_gpon](https://github.com/thienanh95/EN7523_gpon)** —
  EN7523 GPON OpenWrt tree. The Ethernet/DSA patch subset (930-42 through
  930-51) originates from this tree's EN7523 series.

### Patch authors

The kernel patches in `patches/kernel/` carry `From:` and `Signed-off-by:`
lines from their original authors:

- Benjamin Larsson
- Christian Marangi
- Daniel Pawlik
- Daniel Schwierzeck
- Lorenzo Bianconi
- Matheus Sampaio Queiroga (Sirherobrine23)
- Robert Marko
- Sayantan Nandy

### Other credits

- The **mt76 driver maintainers** for the `mt7915e` driver that drives the
  MT7916 WiFi chip
- The **OpenWrt community** for the airoha target framework, build system,
  and LuCI web UI
- **Airoha/Econet** (formerly MediaTek's IoT division) for the EN7523 SoC
  and MT7530 switch documentation that made this port possible
- Community members who confirmed the EEPROM location in `reservearea` at
  offset `0x4c000` across multiple EN7523+MT7916 boards, and noted the
  newer MT7916 WM firmware (20260428) and precal patch availability
