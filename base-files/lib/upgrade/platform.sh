#
# Copyright (C) 2024 OpenWrt.org
#

RAMFS_COPY_BIN='fitblk fit_check_sign'

REQUIRE_IMAGE_METADATA=1

platform_check_image() {
	local board=$(board_name)

	[ "$#" -gt 1 ] && return 1

	case "$board" in
	airoha,en7523-ax3000-router)
		fit_check_image "$1"
		return $?
		;;
	esac

	echo "Unsupported board: $board"
	return 1
}

platform_do_upgrade() {
	local board=$(board_name)

	case "$board" in
	airoha,en7523-ax3000-router)
		fit_do_upgrade "$1"
		;;
	*)
		default_do_upgrade "$1"
		;;
	esac
}

platform_pre_upgrade() {
	sync
}
