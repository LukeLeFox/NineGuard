#import "NGKeychainStore.h"

static NSString *const NGKCService =
    @"dev.luke.nineguard.config.v1";
static NSString *const NGKCAccessGroup =
    @"dev.luke.nineguard.shared";

static NSDictionary *NGKCQuery(NSString *profileID) {

    if (![profileID isKindOfClass:[NSString class]] ||
        ![[NSUUID alloc] initWithUUIDString:profileID])
        return nil;

    return @{
        (__bridge id)kSecClass:
            (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService: NGKCService,
        (__bridge id)kSecAttrAccount: profileID,
        (__bridge id)kSecAttrAccessGroup: NGKCAccessGroup
    };
}

@implementation NGKeychainStore

+ (OSStatus)saveConfigData:(NSData *)data
                profileID:(NSString *)profileID {

    NSDictionary *query = NGKCQuery(profileID);

    if (!query || ![data isKindOfClass:[NSData class]] ||
        data.length == 0 || data.length > 65536)
        return errSecParam;

    NSMutableDictionary *item = [query mutableCopy];

    item[(__bridge id)kSecValueData] = data;
    item[(__bridge id)kSecAttrAccessible] =
        (__bridge id)kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly;

    OSStatus result =
        SecItemAdd((__bridge CFDictionaryRef)item, NULL);

    if (result == errSecDuplicateItem) {
        NSDictionary *update = @{
            (__bridge id)kSecValueData: data
        };

        result = SecItemUpdate(
            (__bridge CFDictionaryRef)query,
            (__bridge CFDictionaryRef)update);
    }

    return result;
}

+ (NSData *)configDataForProfileID:(NSString *)profileID
                            status:(OSStatus *)status {

    if (status) *status = errSecParam;

    NSDictionary *query = NGKCQuery(profileID);
    if (!query) return nil;

    NSMutableDictionary *lookup = [query mutableCopy];

    lookup[(__bridge id)kSecReturnData] = @YES;
    lookup[(__bridge id)kSecMatchLimit] =
        (__bridge id)kSecMatchLimitOne;

    CFTypeRef result = NULL;

    OSStatus code = SecItemCopyMatching(
        (__bridge CFDictionaryRef)lookup, &result);

    if (status) *status = code;

    if (code == errSecSuccess && result)
        return (NSData *)CFBridgingRelease(result);

    if (result) CFRelease(result);
    return nil;
}

+ (OSStatus)removeConfigForProfileID:(NSString *)profileID {

    NSDictionary *query = NGKCQuery(profileID);
    if (!query) return errSecParam;

    return SecItemDelete((__bridge CFDictionaryRef)query);
}

@end
