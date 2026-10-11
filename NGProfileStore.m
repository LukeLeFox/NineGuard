#import "NGProfileStore.h"
#import "NGKeychainStore.h"
#import "NGWGConfigParser.h"

static NSString *const NGActiveProfileIDKey =
    @"NGActiveProfileID";
static NSString *const NGActiveProfilePeerCountKey =
    @"NGActiveProfilePeerCount";

@implementation NGProfileStore

+ (NSString *)activeProfileID {
    NSString *profileID = [[NSUserDefaults standardUserDefaults]
        stringForKey:NGActiveProfileIDKey];
    return [[NSUUID alloc] initWithUUIDString:profileID]
        ? profileID : nil;
}

+ (NSString *)saveConfigurationText:(NSString *)text
                               error:(NSError **)error {
    NSDictionary *config = [NGWGConfigParser parseText:text error:error];
    if (!config)
        return nil;

    NSData *data = [text dataUsingEncoding:NSUTF8StringEncoding];
    if (!data || data.length == 0 || data.length > 65536) {
        if (error) {
            *error = [NSError errorWithDomain:@"dev.luke.nineguard.profile"
                code:1 userInfo:@{NSLocalizedDescriptionKey:
                    @"Configurazione non codificabile"}];
        }
        return nil;
    }

    NSString *profileID = [self activeProfileID]
        ?: [[NSUUID UUID] UUIDString];
    OSStatus status = [NGKeychainStore saveConfigData:data
                                            profileID:profileID];
    if (status != errSecSuccess) {
        if (error) {
            *error = [NSError errorWithDomain:NSOSStatusErrorDomain
                code:status userInfo:@{NSLocalizedDescriptionKey:
                    @"Salvataggio Keychain fallito"}];
        }
        return nil;
    }

    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setObject:profileID forKey:NGActiveProfileIDKey];
    [defaults setInteger:[config[@"peers"] count]
                  forKey:NGActiveProfilePeerCountKey];
    [defaults synchronize];
    return profileID;
}

+ (NSData *)activeConfigurationDataWithStatus:(OSStatus *)status {
    NSString *profileID = [self activeProfileID];
    if (!profileID) {
        if (status) *status = errSecItemNotFound;
        return nil;
    }
    return [NGKeychainStore configDataForProfileID:profileID status:status];
}

+ (OSStatus)removeActiveProfile {
    NSString *profileID = [self activeProfileID];
    OSStatus status = profileID
        ? [NGKeychainStore removeConfigForProfileID:profileID]
        : errSecItemNotFound;
    if (status == errSecSuccess || status == errSecItemNotFound) {
        NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
        [defaults removeObjectForKey:NGActiveProfileIDKey];
        [defaults removeObjectForKey:NGActiveProfilePeerCountKey];
        [defaults synchronize];
    }
    return status;
}

@end
