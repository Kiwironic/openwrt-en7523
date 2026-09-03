# Firmware Extraction Guide

The EN7523 AX3000 router requires three firmware blobs that are **not
redistributed** in this repository. They lack explicit redistribution rights.
You must extract them from your own device's NAND flash and place them in the
build tree before building.

## Required Firmware

| File | Size | Source | Description |
|---|---|---|---|
| `npu_rv32.bin` | 60584 B | `ubifs_slave` partition (UBI volume) | NPU RV32 core firmware |
| `npu_data.bin` | 712 B | `ubifs_slave` partition (UBI volume) | NPU data table |
| `mt7916_eeprom.bin` | 4096 B | `ptdata` partition (UBI volume: `RT30xxEEPROM.bin`, first 4096 bytes) | MT7916 WiFi calibration EEPROM |

## Partition Map (NAND, OpenWrt layout)

From the device tree (`dt/en7523-ax3000-router.dts`) and verified on device:

| Label | MTD | Offset | Size | Notes |
|---|---|---|---|---|
| `bootloader` | mtd0 | 0x0000000 | 0x080000 | U-Boot, read-only |
| `romfile` | mtd1 | 0x0080000 | 0x040000 | read-only |
| `firmware` | mtd2 | 0x00c0000 | 0x3200000 | OpenWrt FIT kernel + squashfs |
| `tclinux_slave` | mtd6 | 0x32c0000 | 0x500000 | vendor backup, read-only |
| `ubifs_slave` | mtd7 | 0x37c0000 | 0x2d00000 | vendor backup, read-only — **NPU firmware source** |
| `ptconf` | mtd8 | 0x64c0000 | 0x600000 | read-only |
| `ptdata` | mtd9 | 0x6ac0000 | 0x400000 | UBI volume — **WiFi EEPROM source** |
| `config` | mtd10 | 0x6ec0000 | 0x100000 | writable |
| `reservearea` | mtd11 | 0x6fc0000 | 0x240000 | read-only (empty on this board) |

## Extraction Procedure

### Prerequisites

- A router running OpenWrt (via TFTP RAM-boot or a previous installation)
- SSH access to the router
- `ubi-reader` on the build host (`pip3 install --user ubi-reader`)

### Step 1: Dump the Source Partitions

```bash
ssh root@<router-ip>

# Dump the partitions containing the firmware
dd if=/dev/mtd7ro of=/tmp/ubifs_slave.bin    # NPU firmware
dd if=/dev/mtd9ro of=/tmp/ptdata.bin         # WiFi EEPROM

# Transfer to build host
exit
scp -O root@<router-ip>:/tmp/ubifs_slave.bin /tmp/
scp -O root@<router-ip>:/tmp/ptdata.bin /tmp/
```

### Step 2: Extract NPU Firmware from ubifs_slave

The `ubifs_slave` partition is a UBI/UBIFS image. Extract its contents:

```bash
ubireader_extract_files -o /tmp/ubi_npu /tmp/ubifs_slave.bin
# The NPU firmware blobs should be in the extracted directory
find /tmp/ubi_npu -name "npu_rv32.bin" -o -name "npu_data.bin"
```

### Step 3: Extract WiFi EEPROM from ptdata

The `ptdata` partition is a UBI image containing the WiFi calibration data
as `RT30xxEEPROM.bin` (208896 bytes). The mt76 driver loads the first 4096
bytes as the EEPROM.

```bash
ubireader_extract_files -o /tmp/ubi_ptdata /tmp/ptdata.bin
# Find RT30xxEEPROM.bin in the extracted directory
find /tmp/ubi_ptdata -name "RT30xxEEPROM.bin"

# Extract the first 4096 bytes (the EEPROM the driver loads)
dd if=/tmp/ubi_ptdata/*/ptdata/RT30xxEEPROM.bin \
   of=mt7916_eeprom.bin bs=4096 count=1
```

### Step 4: Verify Sizes

```bash
ls -l mt7916_eeprom.bin npu_rv32.bin npu_data.bin
# Expected:
# -rw-r--r-- mt7916_eeprom.bin  4096
# -rw-r--r-- npu_rv32.bin      60584
# -rw-r--r-- npu_data.bin       712
```

### Step 5: Place in Build Tree

```bash
# NPU firmware
mkdir -p target/linux/airoha/en7523/base-files/lib/firmware/econet/
cp npu_rv32.bin npu_data.bin \
   target/linux/airoha/en7523/base-files/lib/firmware/econet/

# WiFi EEPROM
mkdir -p target/linux/airoha/en7523/base-files/lib/firmware/mediatek/
cp mt7916_eeprom.bin \
   target/linux/airoha/en7523/base-files/lib/firmware/mediatek/
```

## Loading Calibration from Flash (Preferred)

Instead of shipping a static EEPROM blob, the preferred approach is to load
the MT7916 calibration directly from the art partition at boot time via an
nvmem cell. This requires:

1. Adding an nvmem cell to the ptdata partition in the device tree that covers
   the 4096-byte EEPROM region.
2. Adding an `nvmem-cells` reference to the MT7916 PCIe device node.
3. Removing the fallback to `mediatek/mt7916_eeprom.bin`.

This is the correct long-term fix (see README §3, "EEPROM"). The static blob
approach is a temporary workaround for when the efuse read path fails.

## Legal Note

These firmware blobs are proprietary. They are extracted from your own device
for use on that same device. Do not redistribute them. The license section in
the README does not claim redistribution rights for firmware.
