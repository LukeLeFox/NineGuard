#import "../NGKeychainStore.h"
#import <Foundation/Foundation.h>
#import <NetworkExtension/NetworkExtension.h>

@interface NineGuardTunnelProvider : NEPacketTunnelProvider
@end

@implementation NineGuardTunnelProvider

- (void)startTunnelWithOptions:(NSDictionary *)options
             completionHandler:(void (^)(NSError *))completionHandler {

    NSLog(@"[NineGuard] PacketTunnel START richiesto");

    NSError *error = [NSError errorWithDomain:@"dev.luke.nineguard"
        code:1001
        userInfo:@{
            NSLocalizedDescriptionKey:
                @"NineGuard PoC: tunnel non ancora implementato"
        }];

    completionHandler(error);
}

- (void)stopTunnelWithReason:(NEProviderStopReason)reason
          completionHandler:(void (^)(void))completionHandler {

    NSLog(@"[NineGuard] PacketTunnel STOP");
    completionHandler();
}


- (void)handleAppMessage:(NSData *)messageData
       completionHandler:(void (^)(NSData *))completionHandler {

    if (!completionHandler) return;

    NSString *command = [[NSString alloc]
        initWithData:messageData encoding:NSUTF8StringEncoding];

    if ([command isEqualToString:@"NG-PING-V1"]) {
        NSLog(@"[NineGuardTunnel] IPC PING ricevuto");

        NSData *pong = [@"NG-PONG-V1"
            dataUsingEncoding:NSUTF8StringEncoding];

        completionHandler(pong);
        return;
    }

    if (![command isEqualToString:@"NG-KEYCHAIN-PROBE-V1"]) {
        completionHandler(nil);
        return;
    }

    NSString *probeID =
        @"c0ffeedd-0000-4000-8000-000000000042";

    OSStatus status = errSecParam;
    NSData *data = [NGKeychainStore
        configDataForProfileID:probeID status:&status];

    if (status != errSecSuccess) {
        NSString *message = [NSString stringWithFormat:
            @"ERR:%ld", (long)status];

        completionHandler(
            [message dataUsingEncoding:NSUTF8StringEncoding]);
        return;
    }

    NSString *value = [[NSString alloc]
        initWithData:data encoding:NSUTF8StringEncoding];

    if (data.length > 128 ||
        ![value hasPrefix:@"NGPROBE-v1:"]) {
        completionHandler(nil);
        return;
    }

    completionHandler(data);
}

@end
