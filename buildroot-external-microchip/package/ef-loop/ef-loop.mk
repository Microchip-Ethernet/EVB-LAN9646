################################################################################
#
# ef-loop
#
################################################################################

EF_LOOP_VERSION = 0
EF_LOOP_SITE = $(BR2_EXTERNAL_MCHP_PATH)/cmake_apps/ef-loop
EF_LOOP_SITE_METHOD = local
EF_LOOP_DEPENDENCIES = libev
EF_LOOP_BIN_LICENSE = MIT
EASYRUN_LICENSE_FILES = COPYING

$(eval $(cmake-package))
