export THEOS ?= $(HOME)/theos

include $(THEOS)/makefiles/common.mk
include $(THEOS_MAKE_PATH)/rules.mk

PROJECT_ROOT := $(abspath $(THEOS_PROJECT_DIR)/..)
DAEMON_BUILD := /tmp/nineguard-daemon-build

all::
	@test -x $(PROJECT_ROOT)/Engine/build/wireguard-go || \
		(echo "STOP: eseguire make engine dalla root del progetto"; exit 2)
	$(MAKE) -C $(PROJECT_ROOT)/DaemonPrototype signed

after-stage::
	install -d -m 0755 $(THEOS_STAGING_DIR)/usr/libexec/nineguard
	install -m 0755 $(DAEMON_BUILD)/nineguardd \
		$(THEOS_STAGING_DIR)/usr/libexec/nineguard/nineguardd
	install -m 0755 $(PROJECT_ROOT)/Engine/build/wireguard-go \
		$(THEOS_STAGING_DIR)/usr/libexec/nineguard/wireguard-go
	install -d -m 0755 $(THEOS_STAGING_DIR)/Library/LaunchDaemons
	install -m 0644 \
		$(PROJECT_ROOT)/DaemonPrototype/dev.luke.nineguard.daemon.plist \
		$(THEOS_STAGING_DIR)/Library/LaunchDaemons/
	install -d -m 0755 $(THEOS_STAGING_DIR)/usr/share/doc/nineguard
	install -m 0644 $(PROJECT_ROOT)/Engine/build/COPYING.wireguard-go \
		$(THEOS_STAGING_DIR)/usr/share/doc/nineguard/

clean::
	rm -rf .theos packages
