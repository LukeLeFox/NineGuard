#import "NGKeychainSelfTest.h"
#import "NGKeychainStore.h"

@implementation NGKeychainSelfTest

+ (NSString *)runTest {

    NSString *uuid = [[NSUUID UUID] UUIDString];

    NSData *first =
        [@"NineGuard-DUMMY-A"
         dataUsingEncoding:NSUTF8StringEncoding];

    NSData *second =
        [@"NineGuard-DUMMY-B"
         dataUsingEncoding:NSUTF8StringEncoding];

    OSStatus rc = [NGKeychainStore saveConfigData:first
                                       profileID:uuid];

    if (rc != errSecSuccess)
        return [NSString stringWithFormat:
            @"Scrittura fallita (%ld)", (long)rc];

    NSString *failure = nil;
    OSStatus status = errSecSuccess;

    NSData *read =
        [NGKeychainStore configDataForProfileID:uuid
                                         status:&status];

    if (status != errSecSuccess || ![read isEqualToData:first]) {
        failure = [NSString stringWithFormat:
            @"Lettura fallita (%ld)", (long)status];
    }

    if (!failure) {
        rc = [NGKeychainStore saveConfigData:second
                                  profileID:uuid];

        if (rc != errSecSuccess)
            failure = [NSString stringWithFormat:
                @"Aggiornamento fallito (%ld)", (long)rc];
    }

    if (!failure) {
        read = [NGKeychainStore configDataForProfileID:uuid
                                               status:&status];

        if (status != errSecSuccess ||
            ![read isEqualToData:second])
            failure = [NSString stringWithFormat:
                @"Verifica aggiornamento fallita (%ld)",
                (long)status];
    }

    rc = [NGKeychainStore removeConfigForProfileID:uuid];

    if (rc != errSecSuccess)
        return [NSString stringWithFormat:
            @"Cancellazione fallita (%ld)", (long)rc];

    if (failure) return failure;

    read = [NGKeychainStore configDataForProfileID:uuid
                                           status:&status];

    if (read != nil || status != errSecItemNotFound)
        return [NSString stringWithFormat:
            @"Verifica cancellazione fallita (%ld)",
            (long)status];

    return @"Keychain OK: 4 test superati";
}

@end
