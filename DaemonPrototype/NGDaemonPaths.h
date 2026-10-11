#ifndef NGDAEMONPATHS_H
#define NGDAEMONPATHS_H

#define NG_CONTROL_DIR "/var/run/nineguard"
#define NG_CONTROL_PATH NG_CONTROL_DIR "/nineguardd.sock"
#define NG_ENGINE_PATH "/usr/libexec/nineguard/wireguard-go"
#define NG_ENGINE_IFNAME "utun0"
#define NG_ENGINE_UAPI "/var/run/wireguard/utun0.sock"
#define NG_ENGINE_LOG "/var/log/nineguard-engine.log"

#endif
