#import "NGDaemonProfile.h"
#import "NGKeychainStore.h"
#import "NGWGConfigParser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

int NGDaemonProfileIDValid(const char *profile_id) {
    if (!profile_id || strlen(profile_id) != 36)
        return 0;
    for (size_t i = 0; i < 36; i++) {
        char c = profile_id[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-')
                return 0;
        } else if (!isxdigit((unsigned char)c)) {
            return 0;
        }
    }
    return 1;
}

static const char *NGOptionalUTF8(NSDictionary *section, NSString *key) {
    id value = section[key];
    if (![value isKindOfClass:[NSString class]] || [value length] == 0)
        return NULL;
    return [value UTF8String];
}

void NGDaemonProfileFreeUAPI(char *uapi) {
    if (!uapi)
        return;
    ng_secure_zero(uapi, NG_UAPI_CAPACITY);
    free(uapi);
}

int NGDaemonProfileLoad(const char *profile_id, char **uapi_out,
                        ng_profile_plan *plan) {
    if (!uapi_out || !plan)
        return NG_PROFILE_ERR_ARGS;
    *uapi_out = NULL;
    memset(plan, 0, sizeof(*plan));
    if (!NGDaemonProfileIDValid(profile_id))
        return NG_LOAD_ERR_UUID;

    @autoreleasepool {
        NSString *identifier = [NSString stringWithUTF8String:profile_id];
        if (![[NSUUID alloc] initWithUUIDString:identifier])
            return NG_LOAD_ERR_UUID;

        OSStatus status = errSecParam;
        NSData *data = [NGKeychainStore configDataForProfileID:identifier
                                                        status:&status];
        if (status == errSecItemNotFound)
            return NG_LOAD_ERR_NOT_FOUND;
        if (status != errSecSuccess || !data)
            return NG_LOAD_ERR_KEYCHAIN;

        NSString *text = [[NSString alloc] initWithData:data
                                               encoding:NSUTF8StringEncoding];
        NSError *error = nil;
        NSDictionary *config = text ? [NGWGConfigParser parseText:text
                                                            error:&error]
                                    : nil;
        if (![config isKindOfClass:[NSDictionary class]])
            return NG_LOAD_ERR_PARSE;

        NSDictionary *iface = config[@"interface"];
        NSArray *peers = config[@"peers"];
        if (![iface isKindOfClass:[NSDictionary class]] ||
            ![peers isKindOfClass:[NSArray class]] ||
            peers.count == 0 || peers.count > NG_MAX_PEERS)
            return NG_LOAD_ERR_PARSE;

        ng_peer_input inputs[NG_MAX_PEERS];
        memset(inputs, 0, sizeof(inputs));
        NSUInteger index = 0;
        for (NSDictionary *peer in peers) {
            if (![peer isKindOfClass:[NSDictionary class]])
                return NG_LOAD_ERR_PARSE;
            inputs[index].public_key = NGOptionalUTF8(peer, @"PublicKey");
            inputs[index].preshared_key = NGOptionalUTF8(peer, @"PresharedKey");
            inputs[index].endpoint = NGOptionalUTF8(peer, @"Endpoint");
            inputs[index].keepalive = NGOptionalUTF8(peer,
                                                     @"PersistentKeepalive");
            inputs[index].allowed_ips = NGOptionalUTF8(peer, @"AllowedIPs");
            index++;
        }

        ng_profile_input input;
        memset(&input, 0, sizeof(input));
        input.private_key = NGOptionalUTF8(iface, @"PrivateKey");
        input.listen_port = NGOptionalUTF8(iface, @"ListenPort");
        input.address = NGOptionalUTF8(iface, @"Address");
        input.dns = NGOptionalUTF8(iface, @"DNS");
        input.mtu = NGOptionalUTF8(iface, @"MTU");
        input.peers = inputs;
        input.peer_count = (unsigned)peers.count;

        char *uapi = calloc(1, NG_UAPI_CAPACITY);
        if (!uapi)
            return NG_LOAD_ERR_MEMORY;
        int result = ng_profile_build_uapi(&input, uapi, NG_UAPI_CAPACITY,
                                           plan);
        memset(inputs, 0, sizeof(inputs));
        memset(&input, 0, sizeof(input));
        if (result != NG_PROFILE_OK) {
            NGDaemonProfileFreeUAPI(uapi);
            return result;
        }
        *uapi_out = uapi;
        return NG_LOAD_OK;
    }
}
