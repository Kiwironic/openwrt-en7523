define Target/Description
	Build firmware images for Airoha EN7523 ARM based boards.
endef

define Device/airoha_en7523-evb
  DEVICE_VENDOR := Airoha
  DEVICE_MODEL := EN7523 Evaluation Board
  DEVICE_DTS := en7523-evb
endef
TARGET_DEVICES += airoha_en7523-evb

define Device/airoha_en7523-ax3000-router
  DEVICE_VENDOR := Airoha
  DEVICE_MODEL := EN7523 AX3000 Router
  DEVICE_DTS := en7523-ax3000-router
  DEVICE_PACKAGES := kmod-ubifs kmod-ubi ubi-utils kmod-mtdblock \
	kmod-mt7915e kmod-mt7915-firmware kmod-mt7916-firmware fitblk
endef
TARGET_DEVICES += airoha_en7523-ax3000-router
