include $(TOPDIR)/rules.mk

ARCH:=arm
BOARD:=airoha
BOARDNAME:=Airoha ARM
SUBTARGETS:=en7523 an7581 an7583
FEATURES:=dt squashfs nand ramdisk gpio

KERNEL_PATCHVER:=6.18

include $(INCLUDE_DIR)/target.mk
# kmod-leds-gpio and kmod-gpio-button-hotplug are NOT included here because
# the EN7523 AX3000 router DTS has gpio-leds and gpio-keys-polled nodes built into the
# kernel (CONFIG_LEDS_GPIO=y, CONFIG_KEYBOARD_GPIO_POLLED=y). Loading the
# module versions on top causes "Driver already registered" errors.

$(eval $(call BuildTarget))
