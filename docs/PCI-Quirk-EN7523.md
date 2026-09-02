# PCI Quirk: EN7523 PCIe Root Port BAR 0

## Problem

The EN7523 SoC's PCIe root ports (PCI IDs `14c3:0810` and `14c3:0811`) advertise a 64-bit prefetchable BAR 0 with a size of 8 GiB (`0x200000000`).

On 32-bit ARM, the PCI subsystem cannot assign a 64-bit BAR larger than 4 GiB because the DMA mask is 32 bits. This causes:

1. `pci_setup_device()` logs: `BAR 0: can't handle BAR larger than 4GB (size 0x200000000)`
2. BAR 0 is disabled: `BAR 0 [mem size 0x00000000 64bit pref disabled]`
3. `pci_assign_unassigned_resources()` cannot assign BAR 0
4. `pci_enable_resources()` returns `-EINVAL` because BAR 0 has no assigned resource
5. `pcieport` driver fails to probe the bridge
6. The bridge is never enabled — MMIO writes to devices behind the bridge are silently dropped

## Symptom

The MediaTek MT7916 WiFi driver (`mt7915e`) loads and probes the WiFi endpoint, but all MMIO writes to the radio MCU are lost because the bridge isn't forwarding them. This manifests as:

```
mt7915e 0001:01:00.0: enabling device (0140 -> 0142)
mt7915e 0001:01:00.0: Could not release semaphore
Unable to handle kernel NULL pointer dereference at virtual address 0x4
...
mt76_txq_schedule_pending
```

The "Could not release semaphore" is a 5-second MCU timeout — the host writes the semaphore release register, but the write never reaches the MCU because the PCIe bridge isn't forwarding MMIO.

## Fix

A PCI quirk in `drivers/pci/quirks.c` clears the `IORESOURCE_MEM` flag from BAR 0's resource for these specific root ports. This allows `pci_enable_resources()` to skip the unassignable BAR 0 and proceed to enable the bridge:

```c
static void en7523_pcie_root_port_quirk(struct pci_dev *dev)
{
    struct resource *bar0 = &dev->resource[0];

    if (bar0->flags & IORESOURCE_MEM_64) {
        bar0->flags = 0;
        bar0->start = 0;
        bar0->end = 0;
        pci_info(dev, "EN7523: cleared unassignable 64-bit BAR 0\n");
    }
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_MEDIATEK, 0x0810, en7523_pcie_root_port_quirk);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_MEDIATEK, 0x0811, en7523_pcie_root_port_quirk);
```

## Verification

After applying the quirk, the boot log shows:

```
pcieport 0000:00:00.0: enabling device (0140 -> 0142)
pcieport 0001:00:01.0: enabling device (0140 -> 0142)
```

And the WiFi driver successfully initializes:

```
mt7915e 0001:01:00.0: enabling device (0140 -> 0142)
mt7915e 0001:01:00.0: HW/SW Version: 0x8a108a10, Build Time: 20240823172725a
mt7915e 0001:01:00.0: WM Firmware Version: ____000000, Build Time: 20240823172741
mt7915e 0001:01:00.0: WA Firmware Version: DEV_000000, Build Time: 20240823172837
```

Both `phy0` (2.4GHz) and `phy1` (5GHz) register, and `iw dev wlan0 scan` successfully finds nearby access points.

## Why This Happens

The EN7523 is a 32-bit ARM SoC (Cortex-A53 running in AArch32 mode) with a 32-bit physical address space. The PCIe root port's BAR 0 is a configuration register that the hardware initializes to advertise an 8 GiB prefetchable memory window. On a 64-bit system, this would be assigned normally. On 32-bit ARM, it's impossible to assign, and the PCI core's resource allocation logic treats this as a fatal error for the entire bridge.

The quirk works around this by telling the PCI core to ignore BAR 0 entirely — the bridge doesn't actually need BAR 0 to function; it only needs the bridge memory window (which is assigned separately and correctly).
