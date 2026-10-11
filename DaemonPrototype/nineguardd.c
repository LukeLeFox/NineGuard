#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "NGIPCAuth.h"
#include "NGDaemonPaths.h"
#include "NGDaemonProfile.h"
#include "NGNetConfig.h"
#include "NGDNSConfig.h"
#include "NGNetWatch.h"
#include "NGUAPIClient.h"

extern char **environ;

static volatile sig_atomic_t stop_requested;
static volatile sig_atomic_t child_exited;
static pid_t engine_pid = -1;
static int listen_fd = -1;
static int profile_configured;
static ng_profile_plan active_plan;

/* Watchdog cambio rete: poll ogni NG_WATCH_INTERVAL s mentre un endpoint
 * e' coperto dal tunnel; STOP se il bypass non si ripristina entro
 * NG_WATCH_GRACE s con un router presente. Senza rete si resta chiusi. */
#define NG_WATCH_INTERVAL 5
#define NG_WATCH_GRACE 30
static const char *watch_state = "OK"; /* OK|WAITING|FAILING|STOPPED */
static unsigned watch_rebypass;
static long watch_failing_since;
static time_t watch_last;

static void clear_profile_state(void) {
    profile_configured = 0;
    memset(&active_plan, 0, sizeof(active_plan));
}

static void on_signal(int sig) {
    (void)sig;
    stop_requested = 1;
}

/* Senza SA_RESTART: interrompe accept() e fa pulire subito DNS/bypass. */
static void on_child(int sig) {
    (void)sig;
    child_exited = 1;
}

/* DNS prima delle route: configd smette di pubblicare il servizio. */
static int net_teardown(void) {
    ng_dns_remove();
    return ng_net_down();
}

static int is_socket(const char *path) {
    struct stat status;
    return stat(path, &status) == 0 && S_ISSOCK(status.st_mode);
}

/* Socket UAPI rimasto da un motore terminato con SIGKILL: nessuno ascolta. */
static int remove_stale_uapi(void) {
    if (!is_socket(NG_ENGINE_UAPI))
        return 0;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s",
             NG_ENGINE_UAPI);
    int live = connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0;
    int error = errno;
    close(fd);
    if (live)
        return -2;
    if (error != ECONNREFUSED && error != ENOENT)
        return -2;
    return unlink(NG_ENGINE_UAPI) == 0 || errno == ENOENT ? 0 : -1;
}

static int stop_engine(void);

static void watch_reset(void) {
    watch_state = "OK";
    watch_rebypass = 0;
    watch_failing_since = 0;
}

static void set_watch_state(const char *state) {
    if (strcmp(watch_state, state) != 0)
        printf("NETWATCH=%s REBYPASS=%u\n", state, watch_rebypass);
    watch_state = state;
}

static void watch_network(void) {
    time_t now = time(NULL);
    if (now >= watch_last && now - watch_last < NG_WATCH_INTERVAL)
        return;
    watch_last = now;
    if (engine_pid <= 0 || !ng_net_watch_count())
        return;
    struct in_addr router;
    int have_router = ng_dns_physical_router(&router) == 0;
    ng_net_watch_result watch;
    int result = ng_net_watch(have_router, router, &watch);
    if (watch.replaced || watch.removed) {
        watch_rebypass += watch.replaced + watch.removed;
        printf("NETWATCH=REBYPASS REPLACED=%u REMOVED=%u\n",
               watch.replaced, watch.removed);
    }
    int failing = result == NG_NET_ERR_BYPASS;
    if (ng_watch_grace_expired(&watch_failing_since, failing, (long)now,
                               NG_WATCH_GRACE)) {
        /* Fallback sicuro: niente full tunnel con l'endpoint nel tunnel. */
        stop_engine();
        set_watch_state("STOPPED");
        return;
    }
    set_watch_state(failing ? "FAILING" :
                    result == NG_NET_ERR_NO_GATEWAY ? "WAITING" : "OK");
}

static void refresh_engine(void) {
    if (engine_pid <= 0)
        return;
    int status = 0;
    pid_t result = waitpid(engine_pid, &status, WNOHANG);
    if (result == engine_pid) {
        engine_pid = -1;
        /* Il kernel distrugge utun con indirizzi e route: allinea lo stato
         * e rimuove anche DNS e bypass, che non dipendono dalla utun. */
        net_teardown();
        clear_profile_state();
    }
}

static int wait_engine_exit(int attempts) {
    while (engine_pid > 0 && attempts-- > 0) {
        int status = 0;
        pid_t result = waitpid(engine_pid, &status, WNOHANG);
        if (result == engine_pid) {
            engine_pid = -1;
            return 0;
        }
        if (result < 0 && errno == ECHILD) {
            engine_pid = -1;
            return 0;
        }
        usleep(100000);
    }
    return engine_pid > 0 ? -1 : 0;
}

static int stop_engine(void) {
    int net_result = net_teardown();
    clear_profile_state();
    refresh_engine();
    if (engine_pid <= 0)
        return net_result == NG_NET_OK ? 0 : -1;

    if (kill(engine_pid, SIGTERM) != 0 && errno != ESRCH)
        return -1;
    if (wait_engine_exit(50) != 0) {
        if (kill(engine_pid, SIGKILL) != 0 && errno != ESRCH)
            return -1;
        if (wait_engine_exit(20) != 0)
            return -1;
    }
    return net_result == NG_NET_OK ? 0 : -1;
}

static int start_engine(void) {
    refresh_engine();
    if (engine_pid > 0)
        return 1;
    if (access(NG_ENGINE_PATH, X_OK) != 0)
        return -1;
    if (remove_stale_uapi() != 0)
        return -2;

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0)
        return -1;
    int action_error = 0;
    action_error |= posix_spawn_file_actions_addopen(
        &actions, STDOUT_FILENO, NG_ENGINE_LOG,
        O_WRONLY | O_CREAT | O_TRUNC, 0600);
    action_error |= posix_spawn_file_actions_adddup2(
        &actions, STDOUT_FILENO, STDERR_FILENO);
    if (action_error != 0) {
        posix_spawn_file_actions_destroy(&actions);
        return -1;
    }

    setenv("WG_PROCESS_FOREGROUND", "1", 1);
    setenv("LOG_LEVEL", "error", 1);
    char *const arguments[] = {
        (char *)NG_ENGINE_PATH, (char *)"-f", (char *)NG_ENGINE_IFNAME, NULL
    };
    pid_t child = -1;
    int spawn_error = posix_spawn(&child, NG_ENGINE_PATH, &actions, NULL,
                                  arguments, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawn_error != 0)
        return -1;
    engine_pid = child;

    for (int i = 0; i < 50; i++) {
        refresh_engine();
        if (engine_pid <= 0)
            return -1;
        if (is_socket(NG_ENGINE_UAPI))
            return 0;
        usleep(100000);
    }
    stop_engine();
    return -1;
}

/* START_PROFILE <UUID>: motore + sola configurazione UAPI. */
static void command_start_profile(const char *profile_id,
                                  char *reply, size_t capacity) {
    char *uapi = NULL;
    ng_profile_plan plan;

    refresh_engine();
    if (engine_pid > 0) {
        snprintf(reply, capacity, "START_PROFILE=ALREADY_RUNNING\n");
        return;
    }
    int loaded = NGDaemonProfileLoad(profile_id, &uapi, &plan);
    if (loaded != NG_LOAD_OK) {
        const char *reason = "INVALID";
        if (loaded == NG_LOAD_ERR_UUID) reason = "BAD_UUID";
        else if (loaded == NG_LOAD_ERR_NOT_FOUND) reason = "NOT_FOUND";
        else if (loaded == NG_LOAD_ERR_KEYCHAIN) reason = "KEYCHAIN";
        else if (loaded == NG_LOAD_ERR_PARSE) reason = "PARSE";
        else if (loaded == NG_LOAD_ERR_MEMORY) reason = "MEMORY";
        else if (loaded <= NG_PROFILE_ERR_ARGS)
            reason = ng_profile_error_name(loaded);
        snprintf(reply, capacity, "START_PROFILE=REFUSED_%s\n", reason);
        return;
    }

    int started = start_engine();
    if (started != 0) {
        NGDaemonProfileFreeUAPI(uapi);
        memset(&plan, 0, sizeof(plan));
        snprintf(reply, capacity, "START_PROFILE=%s\n",
                 started == -2 ? "REFUSED_UAPI_BUSY" : "ENGINE_FAILED");
        return;
    }

    int uapi_errno = ng_uapi_set(NG_ENGINE_UAPI, uapi);
    NGDaemonProfileFreeUAPI(uapi);
    uapi = NULL;
    if (uapi_errno != 0) {
        int stopped = stop_engine();
        memset(&plan, 0, sizeof(plan));
        if (uapi_errno == NG_UAPI_ERR_TRANSPORT)
            snprintf(reply, capacity, "START_PROFILE=UAPI_TRANSPORT%s\n",
                     stopped == 0 ? " ROLLBACK=OK" : " ROLLBACK=FAILED");
        else
            snprintf(reply, capacity,
                     "START_PROFILE=UAPI_ERROR ERRNO=%d%s\n", uapi_errno,
                     stopped == 0 ? " ROLLBACK=OK" : " ROLLBACK=FAILED");
        return;
    }

    ng_uapi_stats stats;
    int get_errno = ng_uapi_get_stats(NG_ENGINE_UAPI, &stats);
    if (get_errno != 0 || stats.peers != plan.peer_count ||
        !stats.has_private_key) {
        int stopped = stop_engine();
        memset(&plan, 0, sizeof(plan));
        snprintf(reply, capacity, "START_PROFILE=VERIFY_FAILED%s\n",
                 stopped == 0 ? " ROLLBACK=OK" : " ROLLBACK=FAILED");
        return;
    }

    active_plan = plan;
    profile_configured = 1;
    watch_reset();
    memset(&plan, 0, sizeof(plan));
    snprintf(reply, capacity,
             "START_PROFILE=OK PEERS=%u ALLOWED=%u ENDPOINTS=%u "
             "HOST_ROUTES=%u SKIPPED=%u IPV4=%s\n",
             stats.peers, stats.allowed_ips, stats.endpoints,
             active_plan.host_route_count, active_plan.skipped_allowed_ips,
             active_plan.ipv4_address[0] ? "YES" : "NO");
}

/* NET_UP: IPv4 /32 point-to-point, route degli AllowedIPs IPv4 (/0 come
 * coppia /1 con bypass endpoint) e, solo in full tunnel, DNS del profilo. */
static void command_net_up(char *reply, size_t capacity) {
    refresh_engine();
    if (engine_pid <= 0 || !profile_configured) {
        snprintf(reply, capacity, "NET_UP=REFUSED_NO_PROFILE\n");
        return;
    }
    if (ng_net_is_up()) {
        snprintf(reply, capacity, "NET_UP=ALREADY_UP ROUTES=%u\n",
                 ng_net_route_count());
        return;
    }
    ng_net_result net;
    int result = ng_net_up(NG_ENGINE_IFNAME, &active_plan, &net);
    if (result != NG_NET_OK) {
        const char *reason = "FAILED";
        if (result == NG_NET_ERR_ADDRESS) reason = "ADDRESS";
        else if (result == NG_NET_ERR_ROUTE) reason = "ROUTE";
        else if (result == NG_NET_ERR_NO_INTERFACE) reason = "NO_INTERFACE";
        else if (result == NG_NET_ERR_NO_LINK) reason = "NO_LINK";
        else if (result == NG_NET_ERR_NO_GATEWAY) reason = "NO_GATEWAY";
        else if (result == NG_NET_ERR_BYPASS) reason = "BYPASS";
        else if (result == NG_NET_ERR_MTU) reason = "MTU";
        else if (result == NG_NET_ERR_ARGS) reason = "UNSAFE_PLAN";
        snprintf(reply, capacity, "NET_UP=FAILED_%s ROLLBACK=%s\n", reason,
                 ng_net_is_up() ? "INCOMPLETE" : "OK");
        return;
    }
    const char *dns = "NO";
    if (active_plan.dns_count > 0 && !net.full) {
        dns = "SKIPPED";
    } else if (active_plan.dns_count > 0) {
        if (ng_dns_apply(NG_ENGINE_IFNAME, active_plan.ipv4_address,
                         &active_plan) != NG_DNS_OK) {
            /* Fail closed: niente full tunnel con DNS fuori dal tunnel. */
            int down = net_teardown();
            snprintf(reply, capacity, "NET_UP=FAILED_DNS ROLLBACK=%s\n",
                     down == NG_NET_OK ? "OK" : "INCOMPLETE");
            return;
        }
        dns = "YES";
    }
    snprintf(reply, capacity,
             "NET_UP=OK ROUTES=%u FULL=%s BYPASS=%u DNS=%s SKIPPED=%u "
             "LAN_SKIPPED=%u MTU=%u\n",
             net.routes, net.full ? "YES" : "NO", net.bypass, dns,
             active_plan.skipped_allowed_ips, net.lan_skipped, net.mtu);
}

static void command_stats(char *reply, size_t capacity) {
    refresh_engine();
    if (engine_pid <= 0) {
        snprintf(reply, capacity, "STATS=ENGINE_STOPPED\n");
        return;
    }
    ng_uapi_stats stats;
    int result = ng_uapi_get_stats(NG_ENGINE_UAPI, &stats);
    if (result != 0) {
        if (result == NG_UAPI_ERR_TRANSPORT)
            snprintf(reply, capacity, "STATS=FAILED TRANSPORT\n");
        else
            snprintf(reply, capacity, "STATS=FAILED ERRNO=%d\n", result);
        return;
    }
    char handshake[32];
    if (stats.last_handshake_sec <= 0) {
        snprintf(handshake, sizeof(handshake), "NEVER");
    } else {
        long long age = (long long)time(NULL) - stats.last_handshake_sec;
        snprintf(handshake, sizeof(handshake), "%lld", age < 0 ? 0 : age);
    }
    snprintf(reply, capacity,
             "STATS=OK PEERS=%u HANDSHAKE_AGE=%s TX=%llu RX=%llu NET=%s "
             "FULL=%s DNS=%s\n",
             stats.peers, handshake, (unsigned long long)stats.tx_bytes,
             (unsigned long long)stats.rx_bytes,
             ng_net_is_up() ? "UP" : "DOWN", ng_net_is_full() ? "YES" : "NO",
             ng_dns_is_applied() ? "YES" : "NO");
}

static void handle_client(int client) {
    unsigned char session[256];
    char command[64] = {0}, reply[128] = {0};
    if (NGIPCSessionSize() > sizeof(session) ||
        NGIPCServerReadCommand(client, command, sizeof(command),
                               session, sizeof(session)) != 0) {
        puts("IPC_AUTH=REJECTED");
        return;
    }
    puts("IPC_AUTH=ACCEPTED");

    refresh_engine();
    if (!strcmp(command, "PING")) {
        strcpy(reply, "PONG\n");
    } else if (!strcmp(command, "STATUS")) {
        if (engine_pid > 0)
            snprintf(reply, sizeof(reply),
                     "ENGINE=RUNNING PID=%ld PROFILE=%s NET=%s FULL=%s "
                     "DNS=%s NETWATCH=%s REBYPASS=%u\n",
                     (long)engine_pid, profile_configured ? "YES" : "NO",
                     ng_net_is_up() ? "UP" : "DOWN",
                     ng_net_is_full() ? "YES" : "NO",
                     ng_dns_is_applied() ? "YES" : "NO", watch_state,
                     watch_rebypass);
        else
            snprintf(reply, sizeof(reply),
                     "ENGINE=STOPPED NETWATCH=%s REBYPASS=%u\n",
                     watch_state, watch_rebypass);
    } else if (!strcmp(command, "START")) {
        int result = start_engine();
        if (result == 0)
            strcpy(reply, "START=OK\n");
        else if (result == 1)
            strcpy(reply, "START=ALREADY_RUNNING\n");
        else if (result == -2)
            strcpy(reply, "START=REFUSED_UAPI_BUSY\n");
        else
            strcpy(reply, "START=FAILED\n");
    } else if (!strncmp(command, "START_PROFILE ", 14)) {
        command_start_profile(command + 14, reply, sizeof(reply));
    } else if (!strcmp(command, "NET_UP")) {
        command_net_up(reply, sizeof(reply));
    } else if (!strcmp(command, "NET_DOWN")) {
        strcpy(reply, net_teardown() == NG_NET_OK ? "NET_DOWN=OK\n" :
                                                   "NET_DOWN=FAILED\n");
    } else if (!strcmp(command, "STATS")) {
        command_stats(reply, sizeof(reply));
    } else if (!strcmp(command, "STOP")) {
        strcpy(reply, stop_engine() == 0 ? "STOP=OK\n" :
                                           "STOP=FAILED\n");
    } else if (!strcmp(command, "SHUTDOWN")) {
        strcpy(reply, "SHUTDOWN=OK\n");
    } else {
        strcpy(reply, "ERROR=UNKNOWN_COMMAND\n");
    }
    printf("IPC_REPLY=%s", reply);
    NGIPCServerWriteReply(client, reply, session, sizeof(session));
    memset(session, 0, sizeof(session));
    memset(command, 0, sizeof(command));
    if (!strcmp(reply, "SHUTDOWN=OK\n"))
        stop_requested = 1;
}

static int prepare_control_directory(void) {
    struct stat status;
    if (lstat(NG_CONTROL_DIR, &status) == 0) {
        if (!S_ISDIR(status.st_mode)) {
            puts("STOP: control directory non e una directory");
            return -1;
        }
    } else if (errno == ENOENT) {
        if (mkdir(NG_CONTROL_DIR, 0755) != 0)
            return -1;
    } else {
        return -1;
    }
    return chown(NG_CONTROL_DIR, 0, 0) == 0 &&
           chmod(NG_CONTROL_DIR, 0755) == 0 ? 0 : -1;
}

static int prepare_control_socket(void) {
    struct stat status;
    if (prepare_control_directory() != 0)
        return -1;
    if (lstat(NG_CONTROL_PATH, &status) == 0) {
        if (!S_ISSOCK(status.st_mode)) {
            puts("STOP: control path non e un socket");
            return -1;
        }
        int probe = socket(AF_UNIX, SOCK_STREAM, 0);
        if (probe < 0)
            return -1;
        struct sockaddr_un existing = {0};
        existing.sun_len = sizeof(existing);
        existing.sun_family = AF_UNIX;
        strcpy(existing.sun_path, NG_CONTROL_PATH);
        int active = connect(probe, (struct sockaddr *)&existing,
                             sizeof(existing)) == 0;
        close(probe);
        if (active) {
            puts("STOP: daemon gia attivo");
            return -1;
        }
        if (unlink(NG_CONTROL_PATH) != 0)
            return -1;
    }

    listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0)
        return -1;
    fcntl(listen_fd, F_SETFD, FD_CLOEXEC);

    struct sockaddr_un address = {0};
    address.sun_len = sizeof(address);
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, NG_CONTROL_PATH);
    if (bind(listen_fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        chown(NG_CONTROL_PATH, 0, 501) != 0 ||
        chmod(NG_CONTROL_PATH, 0660) != 0 || listen(listen_fd, 4) != 0)
        return -1;
    return 0;
}

int main(void) {
    if (geteuid() != 0) {
        puts("STOP: nineguardd richiede root");
        return 2;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    signal(SIGPIPE, SIG_IGN);
    struct sigaction action = {0};
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    struct sigaction child_action = {0};
    child_action.sa_handler = on_child;
    sigemptyset(&child_action.sa_mask);
    child_action.sa_flags = SA_NOCLDSTOP;
    sigaction(SIGCHLD, &child_action, NULL);
    /* Bypass rimasti da un'istanza terminata senza NET_DOWN. */
    ng_net_recover();

    if (!NGIPCSecretAvailable()) {
        puts("NINEGUARDD_WAITING_FOR_SECRET=YES");
        while (!stop_requested && !NGIPCSecretAvailable())
            sleep(1);
        if (stop_requested) {
            puts("NINEGUARDD_CLEANUP=OK");
            return 0;
        }
    }

    if (prepare_control_socket() != 0) {
        perror("control socket");
        if (listen_fd >= 0)
            close(listen_fd);
        return 1;
    }
    puts("NINEGUARDD_READY=YES");

    while (!stop_requested) {
        if (child_exited) {
            child_exited = 0;
            refresh_engine();
        }
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(listen_fd, &readable);
        struct timeval timeout = {NG_WATCH_INTERVAL, 0};
        int ready = select(listen_fd + 1, &readable, NULL, NULL,
                           ng_net_watch_count() ? &timeout : NULL);
        if (ready < 0 && errno != EINTR) {
            perror("select");
            break;
        }
        watch_network();
        if (ready <= 0)
            continue;
        int client = accept(listen_fd, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR)
                continue;
            perror("accept");
            break;
        }
        fcntl(client, F_SETFD, FD_CLOEXEC);
        handle_client(client);
        close(client);
    }

    int result = stop_engine();
    close(listen_fd);
    unlink(NG_CONTROL_PATH);
    puts(result == 0 ? "NINEGUARDD_CLEANUP=OK" :
                       "NINEGUARDD_CLEANUP=FAILED");
    return result == 0 ? 0 : 1;
}
