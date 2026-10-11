/*
 * ngudpprobe <IPv4> <porta> [echo]
 * Invia un datagramma UDP "NINEGUARD3" senza vincolare l'interfaccia: la
 * scelta dipende dalla tabella di routing (host route su utun0).
 * Con "echo" attende la stessa risposta per 3 secondi.
 * Nessun IP, route o DNS viene modificato.
 */
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 3 || argc > 4 || (argc == 4 && strcmp(argv[3], "echo"))) {
        puts("USO: ngudpprobe <IPv4> <porta> [echo]");
        return 2;
    }
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_len = sizeof(to);
    to.sin_family = AF_INET;
    long port = strtol(argv[2], NULL, 10);
    if (inet_pton(AF_INET, argv[1], &to.sin_addr) != 1 || port <= 0 ||
        port > 65535) {
        puts("UDP_PROBE=BAD_ARGS");
        return 2;
    }
    to.sin_port = htons((unsigned short)port);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }
    struct timeval timeout = {3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    static const char payload[] = "NINEGUARD3";
    if (sendto(fd, payload, sizeof(payload) - 1, 0, (struct sockaddr *)&to,
               sizeof(to)) != (ssize_t)(sizeof(payload) - 1)) {
        perror("sendto");
        close(fd);
        puts("UDP_SEND=FAILED");
        return 1;
    }
    puts("UDP_SEND=OK");
    if (argc == 4) {
        char buffer[64];
        struct sockaddr_in from;
        socklen_t from_length = sizeof(from);
        ssize_t n = recvfrom(fd, buffer, sizeof(buffer), 0,
                             (struct sockaddr *)&from, &from_length);
        int ok = n == (ssize_t)(sizeof(payload) - 1) &&
                 !memcmp(buffer, payload, sizeof(payload) - 1) &&
                 from.sin_addr.s_addr == to.sin_addr.s_addr &&
                 from.sin_port == to.sin_port;
        puts(ok ? "UDP_ECHO=OK" : (n < 0 ? "UDP_ECHO=TIMEOUT" :
                                           "UDP_ECHO=MISMATCH"));
        close(fd);
        return ok ? 0 : 1;
    }
    close(fd);
    return 0;
}
