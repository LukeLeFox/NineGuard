/*
 * Helper di test (da eseguire come utente mobile, firmato con il gruppo
 * Keychain dev.luke.nineguard.shared). Non stampa mai chiavi o endpoint.
 *
 *   ngprofiletool synthetic        profilo sintetico con chiavi casuali,
 *                                  endpoint TEST-NET 192.0.2.1:51820,
 *                                  AllowedIPs 198.51.100.7/32
 *   ngprofiletool import <file>    importa un .conf gia' esistente
 *   ngprofiletool remove <UUID>    elimina l'elemento Keychain
 *
 * Stampa soltanto PROFILE_ID=<UUID> e l'esito.
 */
#import <Foundation/Foundation.h>
#import <Security/Security.h>
#import "NGKeychainStore.h"
#import "NGWGConfigParser.h"
#include <string.h>
#include <unistd.h>

static NSString *NGRandomKey(void) {
    unsigned char bytes[32];
    if (SecRandomCopyBytes(kSecRandomDefault, sizeof(bytes), bytes) != 0)
        return nil;
    NSData *data = [NSData dataWithBytes:bytes length:sizeof(bytes)];
    memset(bytes, 0, sizeof(bytes));
    return [data base64EncodedStringWithOptions:0];
}

static int NGSave(NSString *text) {
    NSError *error = nil;
    if (![NGWGConfigParser parseText:text error:&error]) {
        printf("PROFILE_PARSE=FAILED %s\n",
               error.localizedDescription.UTF8String ?: "");
        return 1;
    }
    NSString *profileID = [[NSUUID UUID] UUIDString];
    NSData *data = [text dataUsingEncoding:NSUTF8StringEncoding];
    OSStatus status = [NGKeychainStore saveConfigData:data
                                            profileID:profileID];
    if (status != errSecSuccess) {
        printf("PROFILE_SAVE=FAILED STATUS=%ld\n", (long)status);
        return 1;
    }
    printf("PROFILE_ID=%s\n", profileID.UTF8String);
    puts("PROFILE_SAVE=OK");
    return 0;
}

int main(int argc, char **argv) {
    @autoreleasepool {
        if (geteuid() != 501) {
            puts("STOP: eseguire come mobile");
            return 2;
        }
        if (argc == 2 && !strcmp(argv[1], "synthetic")) {
            NSString *privateKey = NGRandomKey();
            NSString *peerKey = NGRandomKey();
            if (!privateKey || !peerKey) {
                puts("RANDOM=FAILED");
                return 1;
            }
            NSString *text = [NSString stringWithFormat:
                @"[Interface]\n"
                 "PrivateKey = %@\n"
                 "Address = 10.253.91.2/32\n"
                 "[Peer]\n"
                 "PublicKey = %@\n"
                 "AllowedIPs = 198.51.100.7/32\n"
                 "Endpoint = 192.0.2.1:51820\n",
                privateKey, peerKey];
            return NGSave(text);
        }
        if (argc == 3 && !strcmp(argv[1], "import")) {
            NSString *path = [NSString stringWithUTF8String:argv[2]];
            NSError *error = nil;
            NSString *text = [NSString stringWithContentsOfFile:path
                encoding:NSUTF8StringEncoding error:&error];
            if (!text) {
                puts("PROFILE_READ=FAILED");
                return 1;
            }
            return NGSave(text);
        }
        if (argc == 3 && !strcmp(argv[1], "remove")) {
            NSString *profileID = [NSString stringWithUTF8String:argv[2]];
            OSStatus status =
                [NGKeychainStore removeConfigForProfileID:profileID];
            printf("PROFILE_REMOVE=%s STATUS=%ld\n",
                   status == errSecSuccess ? "OK" : "FAILED", (long)status);
            return status == errSecSuccess ? 0 : 1;
        }
        puts("USO: ngprofiletool synthetic | import <file> | remove <UUID>");
        return 2;
    }
}
