#include "NGUAPIClient.h"
#include "NGProfileUAPI.h"

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NG_UAPI_RESPONSE_CAPACITY 65536

static int uapi_connect(const char *socket_path) {
    struct sockaddr_un address;
    if (!socket_path || strlen(socket_path) >= sizeof(address.sun_path))
        return -1;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    struct timeval timeout = {3, 0};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
        close(fd);
        return -1;
    }
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    memset(&address, 0, sizeof(address));
#ifdef __APPLE__
    address.sun_len = sizeof(address);
#endif
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, socket_path);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int write_all(int fd, const char *data, size_t length) {
    while (length) {
        ssize_t n = write(fd, data, length);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        data += n;
        length -= (size_t)n;
    }
    return 0;
}

/* Legge fino alla prima riga vuota ("\n\n") o EOF. */
static ssize_t read_response(int fd, char *buffer, size_t capacity) {
    size_t length = 0;
    while (length + 1 < capacity) {
        ssize_t n = read(fd, buffer + length, capacity - 1 - length);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            return -1;
        if (n == 0)
            break;
        length += (size_t)n;
        buffer[length] = '\0';
        if (length >= 2 && strstr(buffer, "\n\n"))
            break;
    }
    buffer[length] = '\0';
    return (ssize_t)length;
}

/* Trova "errno=N" all'inizio di una riga. */
static int parse_errno(const char *response) {
    const char *p = response;
    while (p && *p) {
        if (!strncmp(p, "errno=", 6)) {
            char *end = NULL;
            long value = strtol(p + 6, &end, 10);
            if (end == p + 6 || (*end != '\n' && *end != '\0') ||
                value < -100000 || value > 100000)
                return NG_UAPI_ERR_TRANSPORT;
            return (int)value;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
    return NG_UAPI_ERR_TRANSPORT;
}

int ng_uapi_set(const char *socket_path, const char *body) {
    if (!body || strncmp(body, "set=1\n", 6) != 0)
        return NG_UAPI_ERR_TRANSPORT;
    size_t length = strlen(body);
    if (length < 8 || body[length - 1] != '\n' || body[length - 2] != '\n')
        return NG_UAPI_ERR_TRANSPORT;
    int fd = uapi_connect(socket_path);
    if (fd < 0)
        return NG_UAPI_ERR_TRANSPORT;
    char response[256];
    int result = NG_UAPI_ERR_TRANSPORT;
    if (write_all(fd, body, length) == 0 &&
        read_response(fd, response, sizeof(response)) > 0)
        result = parse_errno(response);
    close(fd);
    ng_secure_zero(response, sizeof(response));
    return result;
}

static int parse_u64(const char *text, uint64_t *value) {
    uint64_t result = 0;
    if (!*text)
        return -1;
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '9')
            return -1;
        result = result * 10 + (uint64_t)(*p - '0');
    }
    *value = result;
    return 0;
}

int ng_uapi_parse_get(const char *response, ng_uapi_stats *stats) {
    if (!response || !stats)
        return NG_UAPI_ERR_TRANSPORT;
    memset(stats, 0, sizeof(*stats));
    int status = parse_errno(response);
    const char *line = response;
    char key[48], value[128];
    while (*line) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        if (length == 0)
            break;
        const char *eq = memchr(line, '=', length);
        if (eq) {
            size_t key_length = (size_t)(eq - line);
            size_t value_length = length - key_length - 1;
            if (key_length < sizeof(key) && value_length < sizeof(value)) {
                memcpy(key, line, key_length);
                key[key_length] = '\0';
                memcpy(value, eq + 1, value_length);
                value[value_length] = '\0';
                uint64_t number = 0;
                if (!strcmp(key, "private_key")) {
                    stats->has_private_key = 1;
                } else if (!strcmp(key, "listen_port")) {
                    if (parse_u64(value, &number) == 0 && number < 65536)
                        stats->listen_port = (unsigned)number;
                } else if (!strcmp(key, "public_key")) {
                    stats->peers++;
                } else if (!strcmp(key, "endpoint")) {
                    stats->endpoints++;
                } else if (!strcmp(key, "allowed_ip")) {
                    stats->allowed_ips++;
                } else if (!strcmp(key, "tx_bytes")) {
                    if (parse_u64(value, &number) == 0)
                        stats->tx_bytes += number;
                } else if (!strcmp(key, "rx_bytes")) {
                    if (parse_u64(value, &number) == 0)
                        stats->rx_bytes += number;
                } else if (!strcmp(key, "last_handshake_time_sec")) {
                    if (parse_u64(value, &number) == 0 &&
                        (int64_t)number > stats->last_handshake_sec)
                        stats->last_handshake_sec = (int64_t)number;
                }
            }
            ng_secure_zero(value, sizeof(value));
        }
        if (!end)
            break;
        line = end + 1;
    }
    return status;
}

int ng_uapi_get_stats(const char *socket_path, ng_uapi_stats *stats) {
    if (!stats)
        return NG_UAPI_ERR_TRANSPORT;
    memset(stats, 0, sizeof(*stats));
    int fd = uapi_connect(socket_path);
    if (fd < 0)
        return NG_UAPI_ERR_TRANSPORT;
    char *response = calloc(1, NG_UAPI_RESPONSE_CAPACITY);
    if (!response) {
        close(fd);
        return NG_UAPI_ERR_TRANSPORT;
    }
    int result = NG_UAPI_ERR_TRANSPORT;
    static const char request[] = "get=1\n\n";
    ssize_t length = -1;
    if (write_all(fd, request, sizeof(request) - 1) == 0)
        length = read_response(fd, response, NG_UAPI_RESPONSE_CAPACITY);
    close(fd);
    if (length > 0)
        result = ng_uapi_parse_get(response, stats);
    ng_secure_zero(response, NG_UAPI_RESPONSE_CAPACITY);
    free(response);
    return result;
}
