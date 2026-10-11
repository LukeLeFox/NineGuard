#ifndef NGIPCAUTH_H
#define NGIPCAUTH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int NGIPCSecretAvailable(void);
int NGIPCProvisionSecret(void);
int NGIPCDeleteSecret(void);

int NGIPCServerReadCommand(int client_fd,
                           char *command, size_t command_capacity,
                           void *session, size_t session_size);
int NGIPCServerWriteReply(int client_fd, const char *reply,
                          const void *session, size_t session_size);

int NGIPCClientExchange(const char *socket_path, const char *command,
                        char *reply, size_t reply_capacity);

size_t NGIPCSessionSize(void);

#ifdef __cplusplus
}
#endif

#endif
