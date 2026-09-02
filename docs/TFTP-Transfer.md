# TFTP RAM-Boot Transfer Method

## Background

The vendor U-Boot on this device has an unreliable TFTP implementation. Whole-file TFTP transfers degrade after ~58-68 KiB of sustained data and stop responding to retransmissions. A 6 MB initramfs image will never complete as a single transfer.

The workaround is to split the image into 32 KiB chunks and transfer them sequentially, each as a separate TFTP request.

## Host Preparation

### TFTP Server

Use a lockstep RFC-1350 TFTP server with:
- 512-byte blocks (no TFTP options/OACK)
- Bound to the test interface IP on UDP port 69

### Network Setup

- Connect the host's Ethernet directly to one of the router's LAN ports
- Host IP: e.g. `192.168.2.126/24`
- Router IP (set in U-Boot): `192.168.2.1`

### Firewall

On Fedora-based hosts, open UDP port 69:
```bash
sudo firewall-cmd --add-port=69/udp
```

Allow unprivileged TFTP server binding:
```bash
sudo sysctl net.ipv4.ip_unprivileged_port_start=68
```

## Transfer Procedure

### 1. Split the Image

```bash
# Split into 32 KiB chunks
dd if=openwrt-*-initramfs-kernel.bin of=chunk000 bs=32768 count=1 skip=0
dd if=openwrt-*-initramfs-kernel.bin of=chunk001 bs=32768 count=1 skip=1
# ... or use split:
split -b 32768 -d -a 3 openwrt-*-initramfs-kernel.bin chunk
```

### 2. Start the TFTP Server

Serve the chunks directory:
```bash
python3 tooling/tftp_lockstep.py --dir chunks/ --bind 192.168.2.126
```

### 3. Load Chunks via U-Boot

At the U-Boot prompt (`ECNT>`), set up networking:
```
setenv ipaddr 192.168.2.1
setenv serverip 192.168.2.126
```

Then load each chunk sequentially. The load address is `0x8a000000 + chunk_index * 0x8000`:

```
tftpboot 0x8a000000 chunk000
tftpboot 0x8a000080 chunk001
tftpboot 0x8a000100 chunk002
...
```

**Important:** U-Boot must be driven one command at a time. This U-Boot build does not accept chained commands.

### 4. Verify Image Integrity

After all chunks are loaded:
```
iminfo 0x8a000000
```

This must show `crc32+ sha1+` for both the kernel and FDT subimages before proceeding.

### 5. Boot

Set bootargs and boot:
```
setenv bootargs console=ttyS0,115200n8 rdinit=/bin/sh
bootm 0x8a000000
```

## Why 0x8a000000?

The initramfs image is ~7 MB. Loading at `0x81800000` (the stock load address) causes the image to overlap its own FIT structure during decompression, resulting in `ERROR: new format image overwritten`. The address `0x8a000000` is above all kernel/initramfs/reserved memory regions used by this DTS.

## Automation

The chunked transfer can be fully automated using the scripts in `tooling/`:

- **`tooling/tftp_lockstep.py`** — the lockstep TFTP server (run this first)
- **`tooling/load_and_boot.py`** — automated serial + TFTP loader that:
  1. Waits for the U-Boot prompt (interrupts autoboot)
  2. Logs in using credentials from `~/.config/uboot-cred`
  3. Sets up networking in U-Boot
  4. Loads all chunks via TFTP (auto-detects count, retries up to 5 times per chunk)
  5. Verifies image integrity with `iminfo`
  6. Boots the image

Usage:
```bash
# Terminal 1: start the TFTP server
python3 tooling/tftp_lockstep.py --dir chunks/ --bind 192.168.2.126

# Terminal 2: power-cycle the router, then run the loader
python3 tooling/load_and_boot.py --port /dev/ttyUSB0 --chunks chunks/
```

The credential file (`~/.config/uboot-cred`) should contain the U-Boot username
on the first line and password on the second line.
