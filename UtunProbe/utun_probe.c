#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * ABI userspace verificata su Apple XNU 3248 / Darwin 15.
 * Gli header privati non sono inclusi nell'SDK pubblico iPhoneOS 9.2.
 */
#define NG_SYSPROTO_CONTROL 2
#define NG_AF_SYS_CONTROL   2
#define NG_MAX_KCTL_NAME    96

#define NG_UTUN_CONTROL_NAME "com.apple.net.utun_control"
#define NG_UTUN_OPT_IFNAME   2

struct ng_ctl_info {
    uint32_t ctl_id;
    char ctl_name[NG_MAX_KCTL_NAME];
};

struct ng_sockaddr_ctl {
    uint8_t  sc_len;
    uint8_t  sc_family;
    uint16_t ss_sysaddr;
    uint32_t sc_id;
    uint32_t sc_unit;
    uint32_t sc_reserved[5];
};

#define NG_CTLIOCGINFO _IOWR('N', 3, struct ng_ctl_info)

static int
ng_fail(const char *stage)
{
    int saved_errno = errno;

    fprintf(stderr,
            "FAIL stage=%s errno=%d (%s)\n",
            stage,
            saved_errno,
            strerror(saved_errno));
    return 1;
}

int
main(void)
{
    struct ng_ctl_info info;
    struct ng_sockaddr_ctl address;
    char interface_name[32];
    socklen_t interface_name_length;
    int fd;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    printf("NineGuard utun probe: euid=%lu\n",
           (unsigned long)geteuid());

    if (sizeof(info) != 100 || sizeof(address) != 32) {
        fprintf(stderr,
                "FAIL ABI: ctl_info=%lu sockaddr_ctl=%lu\n",
                (unsigned long)sizeof(info),
                (unsigned long)sizeof(address));
        return 2;
    }

    fd = socket(PF_SYSTEM, SOCK_DGRAM, NG_SYSPROTO_CONTROL);
    if (fd < 0)
        return ng_fail("socket");

    memset(&info, 0, sizeof(info));
    snprintf(info.ctl_name,
             sizeof(info.ctl_name),
             "%s",
             NG_UTUN_CONTROL_NAME);

    if (ioctl(fd, NG_CTLIOCGINFO, &info) < 0) {
        close(fd);
        return ng_fail("ioctl-CTLIOCGINFO");
    }

    printf("Controller trovato: id=%u\n", info.ctl_id);

    memset(&address, 0, sizeof(address));
    address.sc_len = (uint8_t)sizeof(address);
    address.sc_family = AF_SYSTEM;
    address.ss_sysaddr = NG_AF_SYS_CONTROL;
    address.sc_id = info.ctl_id;
    address.sc_unit = 0;

    if (connect(fd,
                (const struct sockaddr *)&address,
                sizeof(address)) < 0) {
        close(fd);
        return ng_fail("connect-utun");
    }

    memset(interface_name, 0, sizeof(interface_name));
    interface_name_length = sizeof(interface_name);

    if (getsockopt(fd,
                   NG_SYSPROTO_CONTROL,
                   NG_UTUN_OPT_IFNAME,
                   interface_name,
                   &interface_name_length) < 0) {
        close(fd);
        return ng_fail("getsockopt-UTUN_OPT_IFNAME");
    }

    printf("SUCCESS: interfaccia=%s fd=%d\n", interface_name, fd);
    printf("Nessun IP, route o DNS applicato. Attesa 15 secondi...\n");

    sleep(15);

    printf("Chiusura fd: l'interfaccia dovrebbe scomparire.\n");
    close(fd);
    return 0;
}
