# Build Instructions

This document provides detailed step-by-step instructions for building an
OpenWrt image for the EN7523 AX3000 router from this repository.

## Prerequisites

### Build Host

- Linux build host (tested on Fedora, should work on Ubuntu/Debian)
- OpenWrt build dependencies:
  ```bash
  # Fedora
  sudo dnf install ncurses-devel perl-FindBin perl-IPC-Cmd dtc \
    gcc gcc-c++ make flex bison gmp-devel mpfr-devel mpc-devel

  # Ubuntu/Debian
  sudo apt install libncurses-dev libfindbin-libs-perl \
    libipc-cmd-perl device-tree-compiler build-essential \
    flex bison libgmp-dev libmpfr-dev libmpc-dev
  ```
- Space-free build directory path (OpenWrt rejects spaces in `TOPDIR`)
- At least 10 GB free disk space

### Hardware

- EN7523 AX3000 router with UART access (CN3 header, 115200 8N1, 3.3V)
- USB-UART adapter (3.3V logic level)
- Ethernet cable for TFTP recovery (if needed)

## Step 1: Clone OpenWrt

```bash
git clone https://git.openwrt.org/openwrt/openwrt.git
cd openwrt
```

This project is built against the OpenWrt SNAPSHOT branch (kernel 6.18.x).

## Step 2: Install Feeds

Install the LuCI feed and other required packages:

```bash
./scripts/feeds update -a
./scripts/feeds install luci luci-base luci-mod-admin-full \
  luci-theme-bootstrap luci-proto-ipv6 luci-proto-ppp \
  luci-app-firewall luci-app-wireguard
```

## Step 3: Copy Custom Files into the Build Tree

All paths below are relative to the OpenWrt build root. `$REPO` refers to the
root of this repository.

### 3a. Kernel Patches

Copy all patches to the airoha target's patch directory:

```bash
cp $REPO/patches/kernel/*.patch target/linux/airoha/patches-6.18/
```

This includes:
- `001-fix-mt76-led-cflags.patch` — mt76 LED cflags fix (also copy to mt76)
- `203-*.patch` — pinctrl fixes (GPIO direction, SCU IOMUX reset, pin mux)
- `900-*.patch`, `901-*.patch` — BMT (bad block management table) support
- `913-*.patch` — PCIe host bridge reset on init
- `915-*.patch` — Netfilter flowtable offload + HW QoS
- `916-*.patch` — HW GRO TCP support
- `920-*.patch` — Ethernet driver fixes (GDM, MTU, port config)
- `924-*.patch` — NPU coherent mailbox DMA
- `930-*.patch` — EN7523 Ethernet/DSA/PCS patch set (11 patches)
- `950-*.patch` — Econet NPU driver
- `960-*.patch` — PCI quirk for root port BAR 0

### 3b. mt76 LED Patch

The mt76 patch goes in the mt76 package's patch directory:

```bash
cp $REPO/patches/kernel/001-fix-mt76-led-cflags.patch \
   package/kernel/mt76/patches/
```

### 3c. Device Tree

```bash
cp $REPO/dt/en7523-ax3000-router.dts target/linux/airoha/dts/
```

### 3d. NPU Driver

```bash
mkdir -p target/linux/airoha/files/drivers/soc/airoha/
cp $REPO/dt/econet-npu-driver/* target/linux/airoha/files/drivers/soc/airoha/
```

### 3e. NPU Firmware

```bash
mkdir -p target/linux/airoha/en7523/base-files/lib/firmware/econet/
cp $REPO/firmware/npu_data.bin \
   target/linux/airoha/en7523/base-files/lib/firmware/econet/
cp $REPO/firmware/npu_rv32.bin \
   target/linux/airoha/en7523/base-files/lib/firmware/econet/
```

### 3f. WiFi Firmware

```bash
mkdir -p target/linux/airoha/en7523/base-files/lib/firmware/mediatek/
cp $REPO/firmware/mediatek/mt7916_eeprom.bin \
   target/linux/airoha/en7523/base-files/lib/firmware/mediatek/
```

### 3g. Base-files

Copy the board-specific configuration files:

```bash
# LED configuration
cp $REPO/base-files/etc/board.d/01_leds \
   target/linux/airoha/en7523/base-files/etc/board.d/
# Network configuration
cp $REPO/base-files/etc/board.d/02_network \
   target/linux/airoha/en7523/base-files/etc/board.d/

# Hotplug scripts
mkdir -p target/linux/airoha/en7523/base-files/etc/hotplug.d/ieee80211/
cp $REPO/base-files/etc/hotplug.d/ieee80211/15-wifi-led-mux \
   target/linux/airoha/en7523/base-files/etc/hotplug.d/ieee80211/
cp $REPO/base-files/etc/hotplug.d/ieee80211/20-enable-wifi \
   target/linux/airoha/en7523/base-files/etc/hotplug.d/ieee80211/

# UCI defaults
mkdir -p target/linux/airoha/en7523/base-files/etc/uci-defaults/
cp $REPO/base-files/etc/uci-defaults/99-enable-wifi \
   target/linux/airoha/en7523/base-files/etc/uci-defaults/
cp $REPO/base-files/etc/uci-defaults/99-fix-acl-perms \
   target/linux/airoha/en7523/base-files/etc/uci-defaults/

# Upgrade platform script
mkdir -p target/linux/airoha/en7523/base-files/lib/upgrade/
cp $REPO/base-files/lib/upgrade/platform.sh \
   target/linux/airoha/en7523/base-files/lib/upgrade/
```

### 3h. Image Build Files

```bash
cp $REPO/image/Makefile target/linux/airoha/image/Makefile
cp $REPO/image/acl-perms.pseudo target/linux/airoha/image/acl-perms.pseudo
```

### 3i. Target Makefile

```bash
cp $REPO/target-makefile target/linux/airoha/Makefile
```

### 3j. wpad.init Fix

```bash
cp $REPO/package-fixes/wpad.init \
   package/network/services/hostapd/files/wpad.init
```

## Step 4: Configure the Build

```bash
make defconfig
make menuconfig
```

In `menuconfig`, set:

- **Target**: Airoha ARM
- **Subtarget**: EN7523
- **Target Profile**: EN7523 AX3000 Router

Ensure these are selected:
- `CONFIG_ARCH_AIROHA=y` (critical — enables ARM GIC)
- LuCI packages (luci, luci-base, luci-mod-admin-full, luci-theme-bootstrap)
- `apk-mbedtls` (package manager)
- `kmod-mt7915e` or `mt76` (WiFi driver)
- `wireguard-tools`, `luci-app-wireguard` (WireGuard VPN)
- `tcpdump` (packet analyzer)
- `uhttpd` (HTTP server for LuCI)

### Important Config Notes

- `CONFIG_ARCH_AIROHA=y` is **required** — without it, the kernel panics
  immediately because ARM GIC/GICv3 is not enabled.
- The kernel load address is `0x80208000` (set in the image Makefile).
- `kmod-leds-gpio` and `kmod-gpio-button-hotplug` are NOT in DEFAULT_PACKAGES
  because the DTS has built-in LED/key nodes. Loading the module versions on
  top causes "Driver already registered" errors.

## Step 5: Build

```bash
make target/linux/clean
make -j$(nproc)
```

> **NTFS build host gotcha: When editing base-files (e.g. `01_leds`), the build
> system's `root-airoha` directory may retain stale copies. After
> `make package/install`, verify
> `build_dir/target-.../root-airoha/etc/board.d/01_leds` has your changes. If
> not, manually copy the fixed file there, remove
> `build_dir/.../linux-airoha_en7523/root.squashfs`, then run
> `make target/linux/install`.**

The build produces two images in `bin/targets/airoha/en7523/`:

1. **initramfs** (`*-initramfs-kernel.bin`) — for TFTP RAM-boot testing
2. **sysupgrade** (`*-sysupgrade.bin`) — for persistent NAND installation

## Step 6: Re-chunk the Initramfs (for TFTP Boot)

The vendor U-Boot has an unreliable TFTP implementation that degrades after
~58-68 KiB. The initramfs image must be split into 32 KiB chunks:

```bash
mkdir -p chunks
split -b 32768 -d -a 3 \
  bin/targets/airoha/en7523/openwrt-airoha-en7523-*-initramfs-kernel.bin \
  chunks/chunk
```

Each chunk is loaded sequentially via U-Boot TFTP. See
`docs/TFTP-Transfer.md` for the full procedure.

## Step 7: Flashing

> **IMPORTANT: Use `mtd -r write /tmp/sysupgrade.bin firmware` to flash. Do NOT
> use `sysupgrade` on this device — it does not properly write the squashfs
> rootfs. Factory reset (5s reset button) would expose the old squashfs without
> LuCI. `mtd write` overwrites the entire firmware partition (kernel +
> squashfs).**

### Option A: mtd write (from a running OpenWrt)

If OpenWrt is already running (from RAM boot or a previous installation):

```bash
# Transfer the sysupgrade image to the router
scp bin/targets/airoha/en7523/openwrt-airoha-en7523-*-sysupgrade.bin \
  root@192.168.2.1:/tmp/

# SSH in and flash with mtd (NOT sysupgrade)
ssh root@192.168.2.1
mtd -r write /tmp/openwrt-airoha-en7523-*-sysupgrade.bin firmware
```

The router writes the FIT kernel + squashfs rootfs to the `firmware` NAND
partition (50 MiB at offset 0xc0000) and reboots into the persistent
installation.

### Option B: TFTP Recovery (from U-Boot)

If the router is bricked or has no working OpenWrt:

1. Connect via UART to the U-Boot prompt
2. Set up networking in U-Boot:
   ```
   setenv ipaddr 192.168.2.1
   setenv serverip 192.168.2.126
   ```
3. Load each chunk sequentially (one command at a time):
   ```
   tftpboot 0x8a000000 chunk000
   tftpboot 0x8a000080 chunk001
   tftpboot 0x8a000100 chunk002
   ...
   ```
4. Verify image integrity:
   ```
   iminfo 0x8a000000
   ```
   Must show `crc32+ sha1+` for both kernel and FDT subimages.
5. Boot:
   ```
   setenv bootargs console=ttyS0,115200n8 rdinit=/bin/sh
   bootm 0x8a000000
   ```
6. Once OpenWrt is running from RAM, flash the sysupgrade image with `mtd`
   (Option A).

See `docs/TFTP-Transfer.md` for the complete chunked transfer procedure,
including TFTP server setup and automation.

## NTFS Build Host Note

If your build directory is on an NTFS filesystem (e.g., via fuseblk), Unix
permissions are silently ignored — all files show as 0755. This causes ubusd
to reject ACL/capability JSON files (which must be 0644). This is handled two
ways:

1. **Sysupgrade images**: `image/acl-perms.pseudo` sets correct permissions at
   build time via mksquashfs4 pseudo-file entries.
2. **Initramfs boots**: `base-files/etc/uci-defaults/99-fix-acl-perms` runs
   `chmod 0644` on the ACL files at first boot.

Do not waste time trying to fix permissions during package build — they won't
stick on NTFS.
