#import "NGDaemonClient.h"
#import "DaemonPrototype/NGIPCAuth.h"
#import "DaemonPrototype/NGDaemonPaths.h"

static NSString *const NGDaemonClientErrorDomain =
    @"dev.luke.nineguard.daemon-client";

@implementation NGDaemonClient

+ (void)prepareAndPingWithRetries:(NSUInteger)retries
                       completion:(void (^)(NSString *, NSError *))completion {
    [self sendCommand:@"PING" retries:retries completion:completion];
}

+ (void)startProfileID:(NSString *)profileID
            completion:(void (^)(NSString *, NSError *))completion {
    NSString *command = [profileID isKindOfClass:[NSString class]]
        ? [@"START_PROFILE " stringByAppendingString:profileID] : @"";
    [self sendCommand:command retries:1 completion:completion];
}

+ (void)sendCommand:(NSString *)command
             retries:(NSUInteger)retries
          completion:(void (^)(NSString *, NSError *))completion {
    NSSet *allowed = [NSSet setWithArray:@[
        @"PING", @"STATUS", @"START", @"STOP", @"STATS",
        @"NET_UP", @"NET_DOWN"
    ]];
    BOOL startProfile = NO;
    if ([command hasPrefix:@"START_PROFILE "] && command.length == 50) {
        NSString *profileID = [command substringFromIndex:14];
        NSUUID *uuid = [[NSUUID alloc] initWithUUIDString:profileID];
        startProfile = uuid != nil &&
            [uuid.UUIDString caseInsensitiveCompare:profileID] ==
                NSOrderedSame;
    }
    if (!startProfile && ![allowed containsObject:command]) {
        NSError *error = [NSError errorWithDomain:NGDaemonClientErrorDomain
            code:3 userInfo:@{NSLocalizedDescriptionKey:
                @"Comando daemon non consentito"}];
        if (completion) completion(nil, error);
        return;
    }

    dispatch_async(dispatch_get_global_queue(
        DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        if (!NGIPCSecretAvailable() && NGIPCProvisionSecret() != 0) {
            NSError *error = [NSError errorWithDomain:NGDaemonClientErrorDomain
                code:1 userInfo:@{NSLocalizedDescriptionKey:
                    @"Impossibile creare il segreto IPC"}];
            dispatch_async(dispatch_get_main_queue(), ^{
                if (completion) completion(nil, error);
            });
            return;
        }

        NSUInteger attempts = MAX((NSUInteger)1, retries);
        for (NSUInteger attempt = 0; attempt < attempts; attempt++) {
            char reply[128] = {0};
            if (NGIPCClientExchange(
                    NG_CONTROL_PATH,
                    command.UTF8String, reply, sizeof(reply)) == 0) {
                NSString *text = [NSString stringWithUTF8String:reply];
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (completion) completion(text, nil);
                });
                return;
            }
            if (attempt + 1 < attempts)
                [NSThread sleepForTimeInterval:1.0];
        }

        NSError *error = [NSError errorWithDomain:NGDaemonClientErrorDomain
            code:2 userInfo:@{NSLocalizedDescriptionKey:
                @"Demone non raggiungibile o autenticazione fallita"}];
        dispatch_async(dispatch_get_main_queue(), ^{
            if (completion) completion(nil, error);
        });
    });
}

@end
