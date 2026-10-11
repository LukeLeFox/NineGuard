#include "NGIPCAuth.h"
#include "NGDaemonPaths.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    char command[64] = {0};
    if (argc == 3 && !strcmp(argv[1], "START_PROFILE")) {
        if (strlen(argv[2]) != 36) {
            puts("USO: nineguardctl START_PROFILE <UUID>");
            return 2;
        }
        snprintf(command, sizeof(command), "START_PROFILE %s", argv[2]);
    } else if (argc == 2) {
        snprintf(command, sizeof(command), "%s", argv[1]);
    } else {
        puts("USO: nineguardctl PROVISION|DELETE_SECRET|PING|STATUS|START|"
             "STOP|SHUTDOWN|STATS|NET_UP|NET_DOWN|START_PROFILE <UUID>");
        return 2;
    }
    if (!strcmp(argv[1], "PROVISION")) {
        int result = NGIPCProvisionSecret();
        puts(result == 0 ? "PROVISION=OK" : "PROVISION=FAILED");
        return result == 0 ? 0 : 1;
    }
    if (!strcmp(argv[1], "DELETE_SECRET")) {
        int result = NGIPCDeleteSecret();
        puts(result == 0 ? "DELETE_SECRET=OK" : "DELETE_SECRET=FAILED");
        return result == 0 ? 0 : 1;
    }

    char reply[128] = {0};
    if (NGIPCClientExchange(NG_CONTROL_PATH, command,
                            reply, sizeof(reply)) != 0) {
        puts("IPC=AUTH_OR_TRANSPORT_FAILED");
        return 1;
    }
    fputs(reply, stdout);
    return !strncmp(reply, "ERROR=", 6) || strstr(reply, "=FAILED") ||
           strstr(reply, "=REFUSED") || strstr(reply, "=UAPI_ERROR") ||
           strstr(reply, "=VERIFY_FAILED") || strstr(reply, "=ENGINE_FAILED")
           ? 1 : 0;
}
