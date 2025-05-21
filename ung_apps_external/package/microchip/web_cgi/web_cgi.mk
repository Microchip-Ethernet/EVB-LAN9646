################################################################################
#
# web_cgi
#
################################################################################

WEB_CGI_VERSION = 0
WEB_CGI_SITE = $(BR2_EXTERNAL_KSZ_PATH)/app_utils/web_cgi
WEB_CGI_SITE_METHOD = local
WEB_CGI_LICENSE = GPLv2

define WEB_CGI_EXTRACT_CMDS
	cp $(BR2_DL_DIR)/$(WEB_CGI_SOURCE) $(@D)/web_cgi.c
endef

define WEB_CGI_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) \
		-o $(@D)/web.cgi $(@D)/web_cgi.c
endef

define WEB_CGI_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 755 $(@D)/web.cgi \
		$(TARGET_DIR)/var/www/data/cgi-bin/web.cgi
endef

$(eval $(generic-package))
