include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/image.mk

loadaddr-$(CONFIG_TARGET_airoha_an7581) := 0x80200000
loadaddr-$(CONFIG_TARGET_airoha_an7583) := 0x80200000
loadaddr-$(CONFIG_TARGET_airoha_en7523) := 0x80208000

# default all platform image(fit) build
define Device/Default
  PROFILES = Default $$(DEVICE_NAME)
  KERNEL_NAME := Image
  KERNEL = kernel-bin | lzma | \
	fit lzma $$(KDIR)/image-$$(firstword $$(DEVICE_DTS)).dtb
  KERNEL_INITRAMFS = kernel-bin | lzma | \
	fit lzma $$(KDIR)/image-$$(firstword $$(DEVICE_DTS)).dtb
  KERNEL_LOADADDR = $(loadaddr-y)
  FILESYSTEMS := squashfs
  DEVICE_DTS = $$(SOC)-$(lastword $(subst _, ,$(1)))
  DEVICE_DTS_DIR := ../dts
  IMAGES := sysupgrade.bin
  IMAGE/sysupgrade.bin := append-kernel | pad-to 128k | append-rootfs | \
	pad-rootfs | append-metadata
endef

include $(SUBTARGET).mk

# Fix ACL/capabilities file permissions for NTFS build hosts.
# NTFS (fuseblk) doesn't support Unix permissions, so all files show as 0755.
# ubusd rejects ACL files that are not 0644. We use a mksquashfs4 pseudo-file
# to override permissions for all JSON files in acl.d/capabilities at build time.
# (For initramfs boots, see the uci-defaults script 99-fix-acl-perms instead.)
SQUASHFSOPT += -pf $(TOPDIR)/target/linux/airoha/image/acl-perms.pseudo

define Image/Build
	$(call Image/Build/$(1),$(1))
endef

$(eval $(call BuildImage))
