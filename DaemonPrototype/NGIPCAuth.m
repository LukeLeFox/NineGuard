#import "NGIPCAuth.h"
#import <Foundation/Foundation.h>
#import <Security/Security.h>
#import <CommonCrypto/CommonHMAC.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define NGIPC_MAGIC UINT32_C(0x4e474932)
#define NGIPC_MOBILE_ID 501
#define NGIPC_SECRET_SIZE 32
#define NGIPC_NONCE_SIZE 32
#define NGIPC_COMMAND_SIZE 64
#define NGIPC_REPLY_SIZE 128

static NSString *const NGIPCService = @"dev.luke.nineguard.ipc-auth.v1";
static NSString *const NGIPCAccount = @"hmac-key";
static NSString *const NGIPCGroup = @"dev.luke.nineguard.shared";

struct ngipc_challenge {
    uint32_t magic;
    unsigned char nonce[NGIPC_NONCE_SIZE];
};

struct ngipc_request {
    uint32_t magic;
    char command[NGIPC_COMMAND_SIZE];
    unsigned char mac[CC_SHA256_DIGEST_LENGTH];
};

struct ngipc_response {
    uint32_t magic;
    char reply[NGIPC_REPLY_SIZE];
    unsigned char mac[CC_SHA256_DIGEST_LENGTH];
};

struct ngipc_session {
    unsigned char secret[NGIPC_SECRET_SIZE];
    unsigned char nonce[NGIPC_NONCE_SIZE];
    char command[NGIPC_COMMAND_SIZE];
};

static NSDictionary *NGIPCQuery(void) {
    return @{
        (__bridge id)kSecClass:
            (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService: NGIPCService,
        (__bridge id)kSecAttrAccount: NGIPCAccount,
        (__bridge id)kSecAttrAccessGroup: NGIPCGroup
    };
}

static NSData *NGIPCLoadSecret(void) {
    NSMutableDictionary *query = [NGIPCQuery() mutableCopy];
    query[(__bridge id)kSecReturnData] = @YES;
    query[(__bridge id)kSecMatchLimit] =
        (__bridge id)kSecMatchLimitOne;
    CFTypeRef result = NULL;
    OSStatus status = SecItemCopyMatching(
        (__bridge CFDictionaryRef)query, &result);
    if (status != errSecSuccess || !result)
        return nil;
    NSData *secret = [(__bridge NSData *)result copy];
    CFRelease(result);
    return secret.length == NGIPC_SECRET_SIZE ? secret : nil;
}

static int NGIPCTransfer(int fd, void *buffer, size_t length, int writing) {
    unsigned char *cursor = buffer;
    while (length) {
        ssize_t count = writing ? write(fd, cursor, length) :
                                  read(fd, cursor, length);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return -1;
        cursor += count;
        length -= (size_t)count;
    }
    return 0;
}

static int NGIPCSetTimeoutsSeconds(int fd, long receive_seconds) {
    struct timeval receive = {receive_seconds, 0};
    struct timeval send = {3, 0};
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                      &receive, sizeof(receive)) == 0 &&
           setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
                      &send, sizeof(send)) == 0 ? 0 : -1;
}

/* Server: 3 s in entrambe le direzioni (protezione anti-stallo). */
static int NGIPCSetTimeouts(int fd) {
    return NGIPCSetTimeoutsSeconds(fd, 3);
}

/*
 * Client: la risposta a START_PROFILE puo' richiedere avvio del motore
 * (fino a 5 s) e risoluzione DNS dell'endpoint. Il timeout lato server
 * resta 3 s; un client lento non blocca quindi il daemon.
 */
static int NGIPCSetClientTimeouts(int fd) {
    return NGIPCSetTimeoutsSeconds(fd, 20);
}

static void NGIPCHMAC(const unsigned char secret[NGIPC_SECRET_SIZE],
                      const char *domain,
                      const unsigned char nonce[NGIPC_NONCE_SIZE],
                      const char command[NGIPC_COMMAND_SIZE],
                      const char *reply, size_t reply_size,
                      unsigned char output[CC_SHA256_DIGEST_LENGTH]) {
    CCHmacContext context;
    CCHmacInit(&context, kCCHmacAlgSHA256, secret, NGIPC_SECRET_SIZE);
    CCHmacUpdate(&context, domain, strlen(domain));
    CCHmacUpdate(&context, nonce, NGIPC_NONCE_SIZE);
    CCHmacUpdate(&context, command, NGIPC_COMMAND_SIZE);
    if (reply && reply_size)
        CCHmacUpdate(&context, reply, reply_size);
    CCHmacFinal(&context, output);
    memset(&context, 0, sizeof(context));
}

static int NGIPCConstantEqual(const unsigned char *a,
                              const unsigned char *b, size_t length) {
    unsigned char difference = 0;
    for (size_t i = 0; i < length; i++)
        difference |= a[i] ^ b[i];
    return difference == 0;
}

size_t NGIPCSessionSize(void) {
    return sizeof(struct ngipc_session);
}

int NGIPCSecretAvailable(void) {
    @autoreleasepool {
        return NGIPCLoadSecret() != nil;
    }
}

int NGIPCProvisionSecret(void) {
    @autoreleasepool {
        if (geteuid() != NGIPC_MOBILE_ID)
            return -1;
        unsigned char bytes[NGIPC_SECRET_SIZE];
        if (SecRandomCopyBytes(kSecRandomDefault, sizeof(bytes), bytes) != 0)
            return -1;
        SecItemDelete((__bridge CFDictionaryRef)NGIPCQuery());
        NSMutableDictionary *item = [NGIPCQuery() mutableCopy];
        item[(__bridge id)kSecValueData] =
            [NSData dataWithBytes:bytes length:sizeof(bytes)];
        item[(__bridge id)kSecAttrAccessible] =
            (__bridge id)kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly;
        OSStatus status = SecItemAdd((__bridge CFDictionaryRef)item, NULL);
        memset(bytes, 0, sizeof(bytes));
        return status == errSecSuccess ? 0 : -1;
    }
}

int NGIPCDeleteSecret(void) {
    @autoreleasepool {
        if (geteuid() != NGIPC_MOBILE_ID)
            return -1;
        OSStatus status = SecItemDelete(
            (__bridge CFDictionaryRef)NGIPCQuery());
        return status == errSecSuccess || status == errSecItemNotFound ? 0 : -1;
    }
}

int NGIPCServerReadCommand(int client_fd,
                           char *command, size_t command_capacity,
                           void *session, size_t session_size) {
    @autoreleasepool {
        if (!command || command_capacity == 0 ||
            !session || session_size < sizeof(struct ngipc_session))
            return -1;
        if (NGIPCSetTimeouts(client_fd) != 0)
            return -1;
        uid_t uid = (uid_t)-1;
        gid_t gid = (gid_t)-1;
        if (getpeereid(client_fd, &uid, &gid) != 0 ||
            uid != NGIPC_MOBILE_ID || gid != NGIPC_MOBILE_ID)
            return -2;
        NSData *secret = NGIPCLoadSecret();
        if (!secret)
            return -1;

        struct ngipc_session *state = session;
        memset(state, 0, sizeof(*state));
        memcpy(state->secret, secret.bytes, NGIPC_SECRET_SIZE);
        struct ngipc_challenge challenge = {NGIPC_MAGIC, {0}};
        if (SecRandomCopyBytes(kSecRandomDefault,
                               sizeof(challenge.nonce), challenge.nonce) != 0)
            return -1;
        memcpy(state->nonce, challenge.nonce, sizeof(state->nonce));
        if (NGIPCTransfer(client_fd, &challenge, sizeof(challenge), 1) != 0)
            return -1;

        struct ngipc_request request = {0};
        if (NGIPCTransfer(client_fd, &request, sizeof(request), 0) != 0 ||
            request.magic != NGIPC_MAGIC ||
            !memchr(request.command, '\0', sizeof(request.command)) ||
            request.command[0] == '\0')
            return -3;
        unsigned char expected[CC_SHA256_DIGEST_LENGTH];
        NGIPCHMAC(state->secret, "NGIPC-REQ-v1", state->nonce,
                  request.command, NULL, 0, expected);
        int valid = NGIPCConstantEqual(request.mac, expected,
                                       sizeof(expected));
        memset(expected, 0, sizeof(expected));
        if (!valid)
            return -3;
        memcpy(state->command, request.command, sizeof(state->command));
        size_t length = strnlen(request.command, sizeof(request.command));
        if (length >= command_capacity)
            return -1;
        memcpy(command, request.command, length + 1);
        return 0;
    }
}

int NGIPCServerWriteReply(int client_fd, const char *reply,
                          const void *session, size_t session_size) {
    if (!reply || !session || session_size < sizeof(struct ngipc_session))
        return -1;
    size_t length = strlen(reply);
    if (length >= NGIPC_REPLY_SIZE)
        return -1;
    const struct ngipc_session *state = session;
    struct ngipc_response response = {0};
    response.magic = NGIPC_MAGIC;
    memcpy(response.reply, reply, length + 1);
    NGIPCHMAC(state->secret, "NGIPC-RESP-v1", state->nonce,
              state->command, response.reply, sizeof(response.reply),
              response.mac);
    int result = NGIPCTransfer(client_fd, &response, sizeof(response), 1);
    memset(&response, 0, sizeof(response));
    return result;
}

int NGIPCClientExchange(const char *socket_path, const char *command,
                        char *reply, size_t reply_capacity) {
    @autoreleasepool {
        if (geteuid() != NGIPC_MOBILE_ID || !socket_path || !command ||
            !reply || reply_capacity == 0 ||
            strlen(socket_path) >= sizeof(((struct sockaddr_un *)0)->sun_path) ||
            strlen(command) >= NGIPC_COMMAND_SIZE)
            return -1;
        NSData *secret = NGIPCLoadSecret();
        if (!secret)
            return -1;
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
            return -1;
        if (NGIPCSetClientTimeouts(fd) != 0) {
            close(fd);
            return -1;
        }
        struct sockaddr_un address = {0};
        address.sun_len = sizeof(address);
        address.sun_family = AF_UNIX;
        strcpy(address.sun_path, socket_path);
        if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
            close(fd);
            return -1;
        }

        struct ngipc_challenge challenge = {0};
        struct ngipc_request request = {0};
        struct ngipc_response response = {0};
        int result = -1;
        if (NGIPCTransfer(fd, &challenge, sizeof(challenge), 0) != 0 ||
            challenge.magic != NGIPC_MAGIC)
            goto done;
        request.magic = NGIPC_MAGIC;
        strcpy(request.command, command);
        NGIPCHMAC(secret.bytes, "NGIPC-REQ-v1", challenge.nonce,
                  request.command, NULL, 0, request.mac);
        if (NGIPCTransfer(fd, &request, sizeof(request), 1) != 0 ||
            NGIPCTransfer(fd, &response, sizeof(response), 0) != 0 ||
            response.magic != NGIPC_MAGIC ||
            !memchr(response.reply, '\0', sizeof(response.reply)))
            goto done;
        unsigned char expected[CC_SHA256_DIGEST_LENGTH];
        NGIPCHMAC(secret.bytes, "NGIPC-RESP-v1", challenge.nonce,
                  request.command, response.reply, sizeof(response.reply),
                  expected);
        if (!NGIPCConstantEqual(response.mac, expected, sizeof(expected))) {
            memset(expected, 0, sizeof(expected));
            goto done;
        }
        memset(expected, 0, sizeof(expected));
        size_t length = strnlen(response.reply, sizeof(response.reply));
        if (length >= reply_capacity)
            goto done;
        memcpy(reply, response.reply, length + 1);
        result = 0;
done:
        close(fd);
        memset(&challenge, 0, sizeof(challenge));
        memset(&request, 0, sizeof(request));
        memset(&response, 0, sizeof(response));
        return result;
    }
}
