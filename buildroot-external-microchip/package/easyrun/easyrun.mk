################################################################################
#
# easyrun
#
################################################################################

EASYRUN_VERSION = 0
EASYRUN_SITE = $(BR2_EXTERNAL_MCHP_PATH)/cmake_apps/easyrun
EASYRUN_SITE_METHOD = local
EASYRUN_DEPENDENCIES = zlib
EASYRUN_LICENSE = MIT
EASYRUN_LICENSE_FILES = COPYING

$(eval $(cmake-package))
