# Firmware Extraction Guide

The EN7523 AX3000 router requires firmware blobs that are **not
redistributed** in this repository. They lack explicit redistribution rights.
You must extract them from your own device's NAND flash and place them in the
build tree before building.

## Required Firmware

| File | Size | Source | Description |
|---|---|---|---|
| `npu_rv32.bin` | 60584 B | `ubifs_slave` partition (UBI volume) | NPU RV32 core firmware |
| `npu_data.bin` | 712 B | `ubifs_slave` partition (UBI volume) | NPU data table |
| `mt7916_eeprom.bin` | 4096 B | `reservearea` partition at offset `0x4c000` (raw flash) | MT7916 WiFi calibration EEPROM |

> **WiFi EEPROM note:** The DTS includes an nvmem cell that loads the EEPROM
> directly from `reservearea` at boot (see `dt/en7523-ax3000-router.dts`).
> With this nvmem cell, the static `mt7916_eeprom.bin` file is only needed as
> a fallback if the nvmem load fails. If you want to rely solely on the nvmem
> cell, you can skip extracting the EEPROM file entirely.

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
| `ptdata` | mtd9 | 0x6ac0000 | 0x400000 | UBI volume (contains `RT30xxEEPROM.bin`) |
| `config` | mtd10 | 0x6ec0000 | 0x100000 | writable |
| `reservearea` | mtd11 | 0x6fc0000 | 0x240000 | read-only — **WiFi EEPROM at offset 0x4c000** |

## Extraction Procedure

### Prerequisites

- A router running OpenWrt (via TFTP RAM-boot or a previous installation)
- SSH access to the router
- `ubi-reader` on the build host (`pip3 install --user ubi-reader`) — only needed for NPU firmware

### Step 1: Dump the Source Partitions

```bash
ssh root@<router-ip>

# Dump the partitions containing the firmware
dd if=/dev/mtd7ro of=/tmp/ubifs_slave.bin       # NPU firmware
dd if=/dev/mtd11ro of=/tmp/reservearea.bin      # WiFi EEPROM

# Transfer to build host
exit
scp -O root@<router-ip>:/tmp/ubifs_slave.bin /tmp/
scp -O root@<router-ip>:/tmp/reservearea.bin /tmp/
```

### Step 2: Extract NPU Firmware from ubifs_slave

The `ubifs_slave` partition is a UBI/UBIFS image. Extract its contents:

```bash
ubireader_extract_files -o /tmp/ubi_npu /tmp/ubifs_slave.bin
# The NPU firmware blobs should be in the extracted directory
find /tmp/ubi_npu -name "npu_rv32.bin" -o -name "npu_data.bin"
```

### Step 3: Extract WiFi EEPROM from reservearea

The EEPROM is stored as raw data in the `reservearea` partition at offset
`0x4c000` (verified on hardware — the first bytes contain the device MAC
address). Extract the 4096-byte EEPROM:

```bash
dd if=/tmp/reservearea.bin of=mt7916_eeprom.bin bs=1 skip=$((0x4c000)) count=4096
```

> **Alternative:** The EEPROM is also available as `RT30xxEEPROM.bin`
> (208896 bytes) inside a UBI volume on the `ptdata` partition. The first
> 4096 bytes are identical to the `reservearea` data:
> ```bash
> dd if=/dev/mtd9ro of=/tmp/ptdata.bin
> ubireader_extract_files -o /tmp/ubi_ptdata /tmp/ptdata.bin
> dd if=/tmp/ubi_ptdata/*/ptdata/RT30xxEEPROM.bin \
>    of=mt7916_eeprom.bin bs=4096 count=1
> ```

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

# WiFi EEPROM (fallback — see note above about nvmem cell)
mkdir -p target/linux/airoha/en7523/base-files/lib/firmware/mediatek/
cp mt7916_eeprom.bin \
   target/linux/airoha/en7523/base-files/lib/firmware/mediatek/
```

## NVMEM Cell (Implemented)

The DTS (`dt/en7523-ax3000-router.dts`) includes an nvmem cell in the
`reservearea` partition that covers the 4096-byte EEPROM region at offset
`0x4c000`:

```dts
partition@6fc0000 {
    label = "reservearea";
    reg = <0x06fc0000 0x00240000>;
    read-only;

    nvmem-layout {
        compatible = "fixed-layout";
        #address-cells = <1>;
        #size-cells = <1>;

        mt7916_eeprom_factory: eeprom@4c000 {
            reg = <0x4c000 0x1000>;
        };
    };
};
```

The MT7916 PCIe device node references this cell:

```dts
wifi: wifi@0,0 {
    compatible = "mediatek,mt76";
    ...
    nvmem-cells = <&mt7916_eeprom_factory>;
    nvmem-cell-names = "eeprom";
};
```

The mt76 driver tries the nvmem cell first, then efuse, then falls back to
the static `mt7916_eeprom.bin` file. With the nvmem cell in place, the
EEPROM is loaded directly from flash at boot without needing a static blob.

## Legal Note

These firmware blobs are proprietary. They are extracted from your own device
for use on that same device. Do not redistribute them. The license section in
the README does not claim redistribution rights for firmware.
