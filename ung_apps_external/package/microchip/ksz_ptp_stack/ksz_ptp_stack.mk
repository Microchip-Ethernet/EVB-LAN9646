################################################################################
#
# ksz_ptp_stack
#
################################################################################

KSZ_PTP_STACK_VERSION = 0
KSZ_PTP_STACK_SITE = $(BR2_EXTERNAL_KSZ_PATH)/app_utils/ptp_stack
KSZ_PTP_STACK_SITE_METHOD = local
KSZ_PTP_STACK_LICENSE = GPLv2

KSZ_CFLAGS=-Wno-unused-but-set-variable

define KSZ_PTP_STACK_BUILD_CMDS
	CFLAGS="$(TARGET_CFLAGS) $(KSZ_CFLAGS)" \
		CROSS_COMPILE="$(TARGET_CROSS)" \
		$(MAKE) -C $(@D)/.
endef

define KSZ_PTP_STACK_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 755 $(@D)/ptp_stack \
		$(TARGET_DIR)/usr/bin/ptp_stack
endef

$(eval $(generic-package))
