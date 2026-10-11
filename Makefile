export THEOS ?= $(HOME)/theos

TARGET = iphone:clang:9.2:9.0
ARCHS = armv7
TARGET_CODESIGN_FLAGS = -S$(THEOS_PROJECT_DIR)/NineGuard.entitlements

include $(THEOS)/makefiles/common.mk

APPLICATION_NAME = NineGuard
NineGuard_FILES = main.m NGQRScanner.m NGHomeLayoutController.m NGWGConfigParser.m NGKeychainStore.m NGProfileStore.m NGKeychainSelfTest.m NGPublicIPChecker.m NGDaemonClient.m DaemonPrototype/NGIPCAuth.m
NineGuard_CFLAGS = -fobjc-arc $(NINEGUARD_EXTRA_CFLAGS)
NineGuard_FRAMEWORKS = UIKit NetworkExtension AVFoundation Security
NineGuard_INSTALL_PATH = /Applications

include $(THEOS_MAKE_PATH)/application.mk

APPEX_NAME = NineGuardTunnel
NineGuardTunnel_FILES = Tunnel/TunnelProvider.m NGKeychainStore.m
NineGuardTunnel_FRAMEWORKS = Foundation NetworkExtension Security
NineGuardTunnel_CFLAGS = -fobjc-arc
NineGuardTunnel_BUNDLE_RESOURCE_DIRS = Tunnel/Resources
NineGuardTunnel_INSTALL_PATH = /Applications/NineGuard.app/PlugIns

include $(THEOS_MAKE_PATH)/appex.mk

.PHONY: engine

engine:
	./Engine/build-wireguard-go.sh
