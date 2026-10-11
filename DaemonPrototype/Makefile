THEOS ?= $(HOME)/theos
SDK := $(THEOS)/sdks/iPhoneOS9.2.sdk
CLANG := $(THEOS)/toolchain/linux/iphone/bin/clang
LDID := $(THEOS)/toolchain/linux/iphone/bin/ldid
BUILD_DIR ?= /tmp/nineguard-daemon-build

TARGET_FLAGS := -target armv7-apple-ios9.0 -isysroot $(SDK)
WARN_FLAGS := -Os -Wall -Wextra -Werror
AUTH_FRAMEWORKS := -framework Foundation -framework Security
ENTITLEMENTS := NineGuardDaemon.entitlements
ROOT := ..

C_FLAGS := $(TARGET_FLAGS) $(WARN_FLAGS) -std=c11 -I. -I$(ROOT)
OBJC_FLAGS := $(TARGET_FLAGS) $(WARN_FLAGS) -std=gnu11 -fobjc-arc -I. -I$(ROOT)

DAEMON_OBJS := $(BUILD_DIR)/nineguardd.o $(BUILD_DIR)/NGIPCAuth.o \
               $(BUILD_DIR)/NGDaemonProfile.o $(BUILD_DIR)/NGProfileUAPI.o \
               $(BUILD_DIR)/NGUAPIClient.o $(BUILD_DIR)/NGNetConfig.o \
               $(BUILD_DIR)/NGKeychainStore.o $(BUILD_DIR)/NGWGConfigParser.o \
               $(BUILD_DIR)/NGDNSConfig.o $(BUILD_DIR)/NGNetWatch.o

.PHONY: all signed clean

all: $(BUILD_DIR)/nineguardd $(BUILD_DIR)/nineguardctl \
     $(BUILD_DIR)/ngprofiletool $(BUILD_DIR)/ngudpprobe \
     $(BUILD_DIR)/nglistutun $(BUILD_DIR)/ngdnsprobe $(BUILD_DIR)/ngroute

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/nineguardd.o: nineguardd.c NGIPCAuth.h NGDaemonPaths.h \
                           NGDaemonProfile.h NGNetConfig.h NGUAPIClient.h \
                           NGProfileUAPI.h NGDNSConfig.h NGNetWatch.h | $(BUILD_DIR)
	$(CLANG) $(C_FLAGS) -c $< -o $@

$(BUILD_DIR)/nineguardctl.o: nineguardctl.c NGIPCAuth.h NGDaemonPaths.h | $(BUILD_DIR)
	$(CLANG) $(C_FLAGS) -c $< -o $@

$(BUILD_DIR)/NGProfileUAPI.o: NGProfileUAPI.c NGProfileUAPI.h | $(BUILD_DIR)
	$(CLANG) $(C_FLAGS) -c $< -o $@

$(BUILD_DIR)/NGUAPIClient.o: NGUAPIClient.c NGUAPIClient.h NGProfileUAPI.h | $(BUILD_DIR)
	$(CLANG) $(C_FLAGS) -c $< -o $@

$(BUILD_DIR)/NGNetConfig.o: NGNetConfig.c NGNetConfig.h NGProfileUAPI.h NGDaemonPaths.h \
                           NGNetWatch.h | $(BUILD_DIR)
	$(CLANG) $(C_FLAGS) -c $< -o $@

$(BUILD_DIR)/NGNetWatch.o: NGNetWatch.c NGNetWatch.h | $(BUILD_DIR)
	$(CLANG) $(C_FLAGS) -c $< -o $@

$(BUILD_DIR)/NGIPCAuth.o: NGIPCAuth.m NGIPCAuth.h | $(BUILD_DIR)
	$(CLANG) $(OBJC_FLAGS) -c $< -o $@

$(BUILD_DIR)/NGDaemonProfile.o: NGDaemonProfile.m NGDaemonProfile.h \
                                NGProfileUAPI.h | $(BUILD_DIR)
	$(CLANG) $(OBJC_FLAGS) -c $< -o $@

$(BUILD_DIR)/NGKeychainStore.o: $(ROOT)/NGKeychainStore.m \
                                $(ROOT)/NGKeychainStore.h | $(BUILD_DIR)
	$(CLANG) $(OBJC_FLAGS) -c $< -o $@

$(BUILD_DIR)/NGWGConfigParser.o: $(ROOT)/NGWGConfigParser.m \
                                 $(ROOT)/NGWGConfigParser.h | $(BUILD_DIR)
	$(CLANG) $(OBJC_FLAGS) -c $< -o $@

$(BUILD_DIR)/ngprofiletool.o: ngprofiletool.m | $(BUILD_DIR)
	$(CLANG) $(OBJC_FLAGS) -c $< -o $@

$(BUILD_DIR)/nineguardd: $(DAEMON_OBJS)
	$(CLANG) $(TARGET_FLAGS) $^ $(AUTH_FRAMEWORKS) \
		-framework SystemConfiguration -o $@

$(BUILD_DIR)/NGDNSConfig.o: NGDNSConfig.c NGDNSConfig.h NGSCDynamicStore.h \
                            NGProfileUAPI.h | $(BUILD_DIR)
	$(CLANG) $(C_FLAGS) -c $< -o $@

NGROUTE_OBJS := $(BUILD_DIR)/NGNetConfig.o $(BUILD_DIR)/NGNetWatch.o \
                $(BUILD_DIR)/NGDNSConfig.o
$(BUILD_DIR)/ngroute: ngroute.c $(NGROUTE_OBJS) | $(BUILD_DIR)
	$(CLANG) $(C_FLAGS) $< $(NGROUTE_OBJS) -framework CoreFoundation \
		-framework SystemConfiguration -o $@

$(BUILD_DIR)/nineguardctl: $(BUILD_DIR)/nineguardctl.o \
                           $(BUILD_DIR)/NGIPCAuth.o
	$(CLANG) $(TARGET_FLAGS) $^ $(AUTH_FRAMEWORKS) -o $@

$(BUILD_DIR)/ngprofiletool: $(BUILD_DIR)/ngprofiletool.o \
                            $(BUILD_DIR)/NGKeychainStore.o \
                            $(BUILD_DIR)/NGWGConfigParser.o
	$(CLANG) $(TARGET_FLAGS) $^ $(AUTH_FRAMEWORKS) -o $@

$(BUILD_DIR)/ngudpprobe: ngudpprobe.c | $(BUILD_DIR)
	$(CLANG) $(C_FLAGS) $< -o $@

$(BUILD_DIR)/nglistutun: nglistutun.c | $(BUILD_DIR)
	$(CLANG) $(C_FLAGS) $< -o $@

$(BUILD_DIR)/ngdnsprobe: ngdnsprobe.c NGSCDynamicStore.h | $(BUILD_DIR)
	$(CLANG) $(C_FLAGS) $< -framework CoreFoundation \
		-framework SystemConfiguration -o $@

signed: all
	$(LDID) -S$(ENTITLEMENTS) $(BUILD_DIR)/nineguardd
	$(LDID) -S$(ENTITLEMENTS) $(BUILD_DIR)/nineguardctl
	$(LDID) -S$(ENTITLEMENTS) $(BUILD_DIR)/ngprofiletool
	$(LDID) -S $(BUILD_DIR)/ngudpprobe
	$(LDID) -S $(BUILD_DIR)/nglistutun
	$(LDID) -S $(BUILD_DIR)/ngdnsprobe
	$(LDID) -S $(BUILD_DIR)/ngroute

clean:
	rm -f $(DAEMON_OBJS) $(BUILD_DIR)/nineguardctl.o \
		$(BUILD_DIR)/ngprofiletool.o $(BUILD_DIR)/nineguardd \
		$(BUILD_DIR)/nineguardctl $(BUILD_DIR)/ngprofiletool \
		$(BUILD_DIR)/ngudpprobe $(BUILD_DIR)/nglistutun \
		$(BUILD_DIR)/ngdnsprobe $(BUILD_DIR)/ngroute
