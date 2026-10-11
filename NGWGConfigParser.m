#import "NGWGConfigParser.h"

@implementation NGWGConfigParser

+ (NSDictionary *)reject:(NSString *)reason
                   error:(NSError **)error {
    if (error) {
        *error = [NSError errorWithDomain:@"dev.luke.nineguard.parse"
            code:1
            userInfo:@{NSLocalizedDescriptionKey:reason}];
    }
    return nil;
}

+ (BOOL)keyOK:(NSString *)value {
    if (value.length != 44) return NO;

    NSData *data = [[NSData alloc]
        initWithBase64EncodedString:value options:0];

    return data.length == 32;
}

+ (NSDictionary *)parseText:(NSString *)text
                      error:(NSError **)error {

    if (![text isKindOfClass:[NSString class]] ||
        text.length == 0 || text.length > 16384) {
        return [self reject:@"Dimensione configurazione non valida"
                      error:error];
    }

    NSMutableDictionary *iface = [NSMutableDictionary dictionary];
    NSMutableArray *peers = [NSMutableArray array];
    NSMutableDictionary *section = nil;

    BOOL seenInterface = NO;

    NSSet *interfaceKeys = [NSSet setWithArray:@[
        @"PrivateKey", @"Address", @"DNS", @"MTU", @"ListenPort"
    ]];

    NSSet *peerKeys = [NSSet setWithArray:@[
        @"PublicKey", @"PresharedKey", @"AllowedIPs",
        @"Endpoint", @"PersistentKeepalive"
    ]];

    NSCharacterSet *trim =
        [NSCharacterSet whitespaceAndNewlineCharacterSet];

    NSArray *lines = [text componentsSeparatedByCharactersInSet:
        [NSCharacterSet newlineCharacterSet]];

    NSInteger lineNumber = 0;

    for (NSString *raw in lines) {
        lineNumber++;

        NSString *line = [[raw componentsSeparatedByString:@"#"]
            firstObject];
        line = [line stringByTrimmingCharactersInSet:trim];

        if (line.length == 0 || [line hasPrefix:@";"])
            continue;

        if ([line isEqualToString:@"[Interface]"]) {
            if (seenInterface)
                return [self reject:@"Sezione Interface duplicata"
                              error:error];

            seenInterface = YES;
            section = iface;
            continue;
        }

        if ([line isEqualToString:@"[Peer]"]) {
            if (!seenInterface || peers.count >= 32)
                return [self reject:@"Sezione Peer non valida"
                              error:error];

            section = [NSMutableDictionary dictionary];
            [peers addObject:section];
            continue;
        }

        NSRange equal = [line rangeOfString:@"="];

        if (!section || equal.location == NSNotFound)
            return [self reject:
                [NSString stringWithFormat:@"Sintassi alla riga %ld",
                 (long)lineNumber] error:error];

        NSString *key = [[line substringToIndex:equal.location]
            stringByTrimmingCharactersInSet:trim];

        NSString *value = [[line substringFromIndex:equal.location + 1]
            stringByTrimmingCharactersInSet:trim];

        NSSet *allowed = (section == iface)
            ? interfaceKeys : peerKeys;

        if (![allowed containsObject:key] ||
            section[key] != nil || value.length == 0) {
            return [self reject:
                [NSString stringWithFormat:
                 @"Parametro non supportato o duplicato alla riga %ld",
                 (long)lineNumber] error:error];
        }

        section[key] = value;
    }

    if (![self keyOK:iface[@"PrivateKey"]] ||
        ![iface[@"Address"] length] ||
        peers.count == 0) {

        return [self reject:
            @"Interface incompleta o chiave privata non valida"
                      error:error];
    }

    for (NSDictionary *peer in peers) {
        if (![self keyOK:peer[@"PublicKey"]] ||
            ![peer[@"AllowedIPs"] length]) {

            return [self reject:
                @"Peer incompleto o chiave pubblica non valida"
                          error:error];
        }

        NSString *psk = peer[@"PresharedKey"];

        if (psk && ![self keyOK:psk])
            return [self reject:@"PresharedKey non valida"
                          error:error];
    }

    return @{
        @"interface": [iface copy],
        @"peers": [peers copy]
    };
}

@end
