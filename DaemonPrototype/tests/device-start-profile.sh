#!/bin/sh
# Test sul dispositivo reale di START_PROFILE / NET_UP / STATS.
#
# Uso (dalla root del progetto, sul PC):
#   DaemonPrototype/tests/device-start-profile.sh install   # aggiorna il companion .deb
#   DaemonPrototype/tests/device-start-profile.sh uapi      # gate 5: solo UAPI
#   DaemonPrototype/tests/device-start-profile.sh net       # gate 6: IPv4 + host route
#   TESTPEER_CONF=/percorso/client.conf \
#   DaemonPrototype/tests/device-start-profile.sh peer      # gate 7: peer esterno
#   TESTPEER_CONF=... (ngtestpeer -full) \
#   DaemonPrototype/tests/device-start-profile.sh full      # F2: full tunnel + DNS
#   TESTPEER_CONF=... (ngtestpeer -full) \
#   DaemonPrototype/tests/device-start-profile.sh crash     # F2: kill -9 motore/daemon
#   DaemonPrototype/tests/device-start-profile.sh bypass    # F2: bypass endpoint via gateway
#   DaemonPrototype/tests/device-start-profile.sh watch     # watchdog: bypass perso e ripristinato
#   DaemonPrototype/tests/device-start-profile.sh netchange # watchdog + cambio Wi-Fi MANUALE di Luke
#
# netchange: lo script NON tocca il Wi-Fi. Dopo il prompt Luke passa a mano
# l'iPad su un'altra rete (es. hotspot), attende ~45 s e torna al Wi-Fi di
# casa; un logger sul device registra solo tag di 16 bit dei gateway.
#
# Le fasi full/crash cambiano default routing e DNS (richiesto da Luke,
# 11 ott 2026) e li ripristinano; la LAN resta raggiungibile (route /24
# connessa piu' specifica delle /1).
# Regole: nessuna default route, nessun DNS, Wi-Fi non toccato. Ogni fase
# esegue STOP e rimozione del profilo anche in caso di errore. Nessuna
# chiave o configurazione viene stampata.
set -eu

PHASE=${1:-uapi}
IPAD=${IPAD:-root@10.10.99.104}
SSH_OPTS="-o HostKeyAlgorithms=+ssh-rsa -o LogLevel=ERROR"
BUILD_DIR=${BUILD_DIR:-/tmp/nineguard-daemon-build}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
REMOTE=/tmp/nineguard-sp-$(date +%s)
PROFILE_ID=
WATCHDOG_PID=
LOGGER_PID=

rsh() { ssh $SSH_OPTS "$IPAD" "$@"; }
push() { # push <locale> <remoto> <modo>
    rsh "cat > '$2' && chmod $3 '$2'" < "$1"
}
mobile() { rsh "cd /tmp && su mobile -c '$*'"; }
ctl() { mobile "$REMOTE/nineguardctl $*"; }

cleanup() {
    set +e
    if [ "$PHASE" != install ]; then
        echo "--- cleanup"
        if [ -n "$WATCHDOG_PID" ]; then
            rsh "kill $WATCHDOG_PID 2>/dev/null"
        fi
        if [ -n "$LOGGER_PID" ]; then
            rsh "kill $LOGGER_PID 2>/dev/null"
        fi
        ctl NET_DOWN
        ctl STOP
        if [ -n "$PROFILE_ID" ]; then
            mobile "$REMOTE/ngprofiletool remove $PROFILE_ID"
        fi
        rsh "rm -f '$REMOTE/client.conf'"
        rsh "$REMOTE/nglistutun"
        rsh "rm -rf '$REMOTE'"
    fi
}

expect() { # expect <pattern> <testo>
    case "$2" in
        *"$1"*) echo "$2" ;;
        *) echo "$2"; echo "STOP: atteso '$1'"; exit 1 ;;
    esac
}

if ! grep -q NGDaemonProfile "$ROOT/DaemonPrototype/Makefile"; then
    echo "STOP: eseguire prima"
    echo "  cp DaemonPrototype/Makefile.start-profile DaemonPrototype/Makefile"
    exit 2
fi

if [ "$PHASE" = install ]; then
    VERSION=$(sed -n 's/^Version: //p' "$ROOT/DaemonPackage/control")
    make -C "$ROOT/DaemonPackage" clean package
    DEB=$(ls -t "$ROOT"/DaemonPackage/packages/dev.luke.nineguard.daemon_"$VERSION"-*_iphoneos-arm.deb | head -1)
    sha256sum "$DEB"
    RDEB=/tmp/nineguard-daemon-$VERSION-$(date +%s).deb
    push "$DEB" "$RDEB" 0644
    rsh "dpkg -i $RDEB >/dev/null && \
         dpkg-query -W -f='\${Package} \${Version} \${Status}\n' dev.luke.nineguard.daemon && \
         rm -f $RDEB"
    echo "INSTALL_PHASE=OK (aprire l'app se il socket non compare)"
    exit 0
fi

make -C "$ROOT/DaemonPrototype" signed BUILD_DIR="$BUILD_DIR" >/dev/null
rsh "mkdir -m 0755 '$REMOTE'"
for tool in nineguardctl ngprofiletool ngudpprobe nglistutun ngroute; do
    push "$BUILD_DIR/$tool" "$REMOTE/$tool" 0755
done
trap cleanup EXIT INT TERM

echo "--- preflight"
rsh "dpkg-query -W -f='\${Version}\n' dev.luke.nineguard.daemon"
expect "PONG" "$(ctl PING)"
expect "ENGINE=STOPPED" "$(ctl STATUS)"
expect "UTUN_INTERFACES=0" "$(rsh "$REMOTE/nglistutun")"

echo "--- profilo"
scdns() { # 1 se il DNS globale di sistema e' quello del tunnel
    rsh "printf 'show State:/Network/Global/DNS\nquit\n' | scutil | grep -c 10.253.92.1" || true
}
svc() { # chiavi del servizio NineGuard in configd
    rsh "printf 'list State:/Network/Service/dev.luke.nineguard/.*\nquit\n' | scutil | grep -c subKey" || true
}

SYNTH_BYPASS=0
case "$PHASE" in bypass|watch|netchange) SYNTH_BYPASS=1 ;; esac

if [ "$SYNTH_BYPASS" = 1 ]; then
    # Profilo full sintetico: chiavi casuali, endpoint TEST-NET oltre il
    # gateway (nessun handshake atteso). File locale 0600, mai stampato.
    LOCAL_CONF=$(mktemp /tmp/nineguard-bypass.XXXXXX)
    python3 -c 'import os,base64
k=lambda:base64.b64encode(os.urandom(32)).decode()
print("[Interface]\nPrivateKey = %s\nAddress = 10.253.93.2/32\nDNS = 10.253.93.1\nMTU = 1280\n[Peer]\nPublicKey = %s\nAllowedIPs = 0.0.0.0/0\nEndpoint = 192.0.2.1:51820" % (k(), k()))' > "$LOCAL_CONF"
    TESTPEER_CONF=$LOCAL_CONF
fi

if [ "$PHASE" = peer ] || [ "$PHASE" = full ] || [ "$PHASE" = crash ] || [ "$SYNTH_BYPASS" = 1 ]; then
    [ -n "${TESTPEER_CONF:-}" ] && [ -f "$TESTPEER_CONF" ] || {
        echo "STOP: impostare TESTPEER_CONF (file generato da ngtestpeer)"; exit 2; }
    push "$TESTPEER_CONF" "$REMOTE/client.conf" 0600
    rsh "chown mobile:mobile '$REMOTE/client.conf'"
    OUT=$(mobile "$REMOTE/ngprofiletool import $REMOTE/client.conf") || true
    rsh "rm -f '$REMOTE/client.conf'"
    [ -n "${LOCAL_CONF:-}" ] && rm -f "$LOCAL_CONF"
else
    OUT=$(mobile "$REMOTE/ngprofiletool synthetic") || true
fi
PROFILE_ID=$(printf '%s\n' "$OUT" | sed -n 's/^PROFILE_ID=//p')
expect "PROFILE_SAVE=OK" "$OUT"

echo "--- START_PROFILE (solo UAPI)"
expect "START_PROFILE=OK PEERS=1" "$(ctl START_PROFILE "$PROFILE_ID")"
expect "PROFILE=YES NET=DOWN" "$(ctl STATUS)"
expect "STATS=OK PEERS=1" "$(ctl STATS)"
expect "UTUN_IPV4_ADDRESSES=0" "$(rsh "$REMOTE/nglistutun")"

if [ "$PHASE" = full ] || [ "$PHASE" = crash ] || [ "$SYNTH_BYPASS" = 1 ]; then
    # Paracadute sul device: STOP automatico anche se l'SSH cade.
    GUARD=120
    [ "$PHASE" = netchange ] && GUARD=600
    WATCHDOG_PID=$(rsh "cd /tmp; nohup sh -c 'sleep $GUARD; su mobile -c \"$REMOTE/nineguardctl STOP\"' >/dev/null 2>&1 & echo \$!")
    echo "WATCHDOG_PID_SET=$([ -n "$WATCHDOG_PID" ] && echo YES || echo NO)"
fi

if [ "$PHASE" = full ]; then
    echo "--- baseline"
    expect "IF=en0" "$(rsh "$REMOTE/ngroute get 1.1.1.1")"
    expect "FAILED" "$(mobile "$REMOTE/ngroute resolve probe.nineguard.test" || true)"
    echo "--- NET_UP full"
    expect "NET_UP=OK" "$(ctl NET_UP)"
    expect "FULL=YES" "$(ctl STATUS)"
    expect "IF=utun0" "$(rsh "$REMOTE/ngroute get 1.1.1.1 8.8.8.8")"
    expect "IF=en0" "$(rsh "$REMOTE/ngroute get 10.10.99.2")"
    echo "SCUTIL_SERVICE_KEYS=$(svc) GLOBAL_DNS_VPN=$(scdns)"
    rsh "printf 'show State:/Network/Global/IPv4\nquit\n' | scutil | grep PrimaryInterface"
    expect "UDP_ECHO=OK" "$(rsh "$REMOTE/ngudpprobe 10.253.92.1 7 echo")"
    expect "A=10.253.92.1" "$(mobile "$REMOTE/ngroute resolve probe.nineguard.test")"
    expect "UDP_SEND=OK" "$(rsh "$REMOTE/ngudpprobe 1.1.1.1 9")"
    expect "FULL=YES DNS=YES" "$(ctl STATS)"
    echo "--- NET_DOWN"
    expect "NET_DOWN=OK" "$(ctl NET_DOWN)"
    expect "IF=en0" "$(rsh "$REMOTE/ngroute get 1.1.1.1")"
    sleep 1
    echo "SCUTIL_SERVICE_KEYS=$(svc) GLOBAL_DNS_VPN=$(scdns)"
    [ "$(svc)" = 0 ] && [ "$(scdns)" = 0 ] || { echo "STOP: DNS non ripristinato"; exit 1; }
    expect "FAILED" "$(mobile "$REMOTE/ngroute resolve probe2.nineguard.test" || true)"
fi

if [ "$PHASE" = bypass ]; then
    echo "--- NET_UP full con endpoint oltre il gateway"
    expect "STATIC_HOST=NO" "$(rsh "$REMOTE/ngroute get 192.0.2.1")"
    expect "BYPASS=1 DNS=YES SKIPPED=0 LAN_SKIPPED=0 MTU=1280" "$(ctl NET_UP)"
    expect "MTU=1280" "$(rsh "$REMOTE/nglistutun")"
    expect "IF=en0 GW=YES STATIC_HOST=YES" "$(rsh "$REMOTE/ngroute get 192.0.2.1")"
    expect "IF=utun0" "$(rsh "$REMOTE/ngroute get 192.0.2.2 1.1.1.1")"
    expect "STATE=1" "STATE=$(rsh "test -f /var/run/nineguard/net.state && echo 1 || echo 0")"
    expect "NET_DOWN=OK" "$(ctl NET_DOWN)"
    expect "MTU=1420" "$(rsh "$REMOTE/nglistutun")"
    expect "IF=en0 GW=YES STATIC_HOST=NO" "$(rsh "$REMOTE/ngroute get 192.0.2.1")"
    expect "STATE=0" "STATE=$(rsh "test -f /var/run/nineguard/net.state && echo 1 || echo 0")"
    echo "--- bypass dopo crash daemon"
    expect "NET_UP=OK" "$(ctl NET_UP)"
    expect "STATIC_HOST=YES" "$(rsh "$REMOTE/ngroute get 192.0.2.1")"
    rsh "killall -9 nineguardd"
    sleep 8
    expect "PONG" "$(ctl PING)"
    expect "IF=en0 GW=YES STATIC_HOST=NO" "$(rsh "$REMOTE/ngroute get 192.0.2.1 1.1.1.1")"
    expect "STATE=0" "STATE=$(rsh "test -f /var/run/nineguard/net.state && echo 1 || echo 0")"
    echo "SCUTIL_SERVICE_KEYS=$(svc) GLOBAL_DNS_VPN=$(scdns)"
    expect "START_PROFILE=OK" "$(ctl START_PROFILE "$PROFILE_ID")"
fi

if [ "$PHASE" = watch ] || [ "$PHASE" = netchange ]; then
    echo "--- watchdog: bypass rimosso dal kernel (simulato)"
    expect "BYPASS=1" "$(ctl NET_UP)"
    expect "BYPASS=YES" "$(rsh "$REMOTE/ngroute gwcheck 192.0.2.1")"
    expect "MATCH=YES" "$(rsh "$REMOTE/ngroute gwcheck 192.0.2.1")"
    expect "NETWATCH=OK REBYPASS=0" "$(ctl STATUS)"
    expect "DELETE 192.0.2.1 OK" "$(rsh "$REMOTE/ngroute delete 192.0.2.1")"
    expect "IF=utun0" "$(rsh "$REMOTE/ngroute get 192.0.2.1")"
    sleep 8
    expect "BYPASS=YES" "$(rsh "$REMOTE/ngroute gwcheck 192.0.2.1")"
    expect "MATCH=YES" "$(rsh "$REMOTE/ngroute gwcheck 192.0.2.1")"
    expect "NETWATCH=OK REBYPASS=1" "$(ctl STATUS)"
    expect "STATE=1" "STATE=$(rsh "test -f /var/run/nineguard/net.state && echo 1 || echo 0")"
    expect "IF=utun0" "$(rsh "$REMOTE/ngroute get 1.1.1.1")"
fi

if [ "$PHASE" = netchange ]; then
    HOME_TAG=$(rsh "$REMOTE/ngroute gwcheck 192.0.2.1" | sed -n 's/.*GWTAG=\([0-9a-f]*\).*/\1/p')
    LOGGER=$(mktemp /tmp/nineguard-netlog.XXXXXX)
    cat > "$LOGGER" <<'LOG'
#!/bin/sh
# Logger sul device: solo esiti e tag, niente IP. 150 x 3 s = 450 s.
R=$1; i=0
while [ $i -lt 150 ]; do
    echo "T=$(date +%s) $($R/ngroute gwcheck 192.0.2.1) $($R/ngroute get 1.1.1.1) $(cd /tmp && su mobile -c "$R/nineguardctl STATUS")"
    sleep 3; i=$((i + 1))
done
LOG
    push "$LOGGER" "$REMOTE/netlog.sh" 0755
    rm -f "$LOGGER"
    LOGGER_PID=$(rsh "cd /tmp; nohup $REMOTE/netlog.sh $REMOTE > $REMOTE/netchange.log 2>&1 & echo \$!")
    # Niente BatchMode: su questo iPad fa fallire l'autenticazione.
    up() { ssh $SSH_OPTS -o ConnectTimeout=3 "$IPAD" true </dev/null 2>/dev/null; }
    echo "=== LUKE: ora passa A MANO l'iPad su un'altra rete Wi-Fi (es. hotspot),"
    echo "=== attendi ~45 s, poi torna al Wi-Fi di casa. Hai 4 minuti per iniziare."
    waited=0
    while up; do
        sleep 3; waited=$((waited + 3))
        [ $waited -lt 240 ] || { echo "STOP: nessun cambio rete rilevato"; exit 1; }
    done
    echo "NETCHANGE_SSH_LOST_AFTER=${waited}s"
    waited=0
    until up; do
        sleep 3; waited=$((waited + 3))
        [ $waited -lt 360 ] || { echo "STOP: iPad non rientrato (paracadute STOP a 600 s)"; exit 1; }
    done
    echo "NETCHANGE_SSH_BACK_AFTER=${waited}s"
    sleep 10
    rsh "kill $LOGGER_PID 2>/dev/null" || true
    LOGGER_PID=
    echo "--- log (tag gateway, transizioni)"
    rsh "cut -d' ' -f2- '$REMOTE/netchange.log' | sed 's/ PID=[0-9]*//' | uniq -c"
    OTHER=$(rsh "grep 'BYPASS=YES' '$REMOTE/netchange.log' | grep 'MATCH=YES' | grep 'ROUTE 1.1.1.1 IF=utun0' | grep -vc 'GWTAG=$HOME_TAG'" || true)
    echo "NETCHANGE_REBYPASS_OTHER_NET_SAMPLES=$OTHER"
    [ "${OTHER:-0}" -gt 0 ] || { echo "STOP: bypass non spostato sulla nuova rete"; exit 1; }
    expect "GWTAG=$HOME_TAG" "$(rsh "$REMOTE/ngroute gwcheck 192.0.2.1")"
    expect "MATCH=YES" "$(rsh "$REMOTE/ngroute gwcheck 192.0.2.1")"
    expect "IF=utun0" "$(rsh "$REMOTE/ngroute get 1.1.1.1")"
    ST=$(ctl STATUS)
    expect "NETWATCH=OK" "$ST"
    N=$(printf '%s\n' "$ST" | sed -n 's/.*REBYPASS=\([0-9]*\).*/\1/p')
    [ "${N:-0}" -ge 3 ] || { echo "STOP: atteso REBYPASS>=3 (simulato + andata + ritorno)"; exit 1; }
    expect "NET_DOWN=OK" "$(ctl NET_DOWN)"
    expect "IF=en0 GW=YES STATIC_HOST=NO" "$(rsh "$REMOTE/ngroute get 192.0.2.1 1.1.1.1")"
    expect "STATE=0" "STATE=$(rsh "test -f /var/run/nineguard/net.state && echo 1 || echo 0")"
fi

if [ "$PHASE" = crash ]; then
    echo "--- crash motore"
    expect "NET_UP=OK" "$(ctl NET_UP)"
    expect "FULL=YES" "$(ctl STATUS)"
    rsh "killall -9 wireguard-go"
    sleep 2
    expect "IF=en0" "$(rsh "$REMOTE/ngroute get 1.1.1.1")"
    echo "SCUTIL_SERVICE_KEYS=$(svc) GLOBAL_DNS_VPN=$(scdns)"
    [ "$(svc)" = 0 ] && [ "$(scdns)" = 0 ] || { echo "STOP: DNS rimasto dopo crash motore"; exit 1; }
    expect "ENGINE=STOPPED" "$(ctl STATUS)"
    echo "--- crash daemon"
    expect "START_PROFILE=OK" "$(ctl START_PROFILE "$PROFILE_ID")"
    expect "NET_UP=OK" "$(ctl NET_UP)"
    rsh "killall -9 nineguardd"
    sleep 8
    expect "IF=en0" "$(rsh "$REMOTE/ngroute get 1.1.1.1")"
    echo "SCUTIL_SERVICE_KEYS=$(svc) GLOBAL_DNS_VPN=$(scdns)"
    [ "$(svc)" = 0 ] && [ "$(scdns)" = 0 ] || { echo "STOP: DNS rimasto dopo crash daemon"; exit 1; }
    echo "ENGINE_PROCESSES=$(rsh 'ps ax | grep -c "[w]ireguard-go -f"' || true)"
    expect "UTUN_INTERFACES=0" "$(rsh "$REMOTE/nglistutun")"
    expect "PONG" "$(ctl PING)"
    expect "ENGINE=STOPPED" "$(ctl STATUS)"
fi

if [ "$PHASE" = net ] || [ "$PHASE" = peer ]; then
    echo "--- NET_UP (IPv4 /32 + host route /32, niente default/DNS)"
    expect "NET_UP=OK ROUTES=1" "$(ctl NET_UP)"
    expect "MTU=1420" "$(rsh "$REMOTE/nglistutun")"
    expect "UTUN_IPV4_ADDRESSES=1" "$(rsh "$REMOTE/nglistutun")"
    if [ "$PHASE" = net ]; then
        # 198.51.100.7 e' l'unica host route del profilo sintetico; il peer
        # sintetico (TEST-NET) non risponde: atteso TX>0, handshake NEVER.
        expect "UDP_SEND=OK" "$(rsh "$REMOTE/ngudpprobe 198.51.100.7 9")"
        sleep 2
        expect "HANDSHAKE_AGE=NEVER" "$(ctl STATS)"
    else
        expect "UDP_ECHO=OK" "$(rsh "$REMOTE/ngudpprobe 10.253.92.1 7 echo")"
        expect "STATS=OK PEERS=1 HANDSHAKE_AGE=" "$(ctl STATS)"
        case "$(ctl STATS)" in *HANDSHAKE_AGE=NEVER*) echo "STOP: nessun handshake"; exit 1;; esac
    fi
    expect "NET_DOWN=OK" "$(ctl NET_DOWN)"
    expect "UTUN_IPV4_ADDRESSES=0" "$(rsh "$REMOTE/nglistutun")"
fi

echo "--- STOP"
expect "STOP=OK" "$(ctl STOP)"
expect "UTUN_INTERFACES=0" "$(rsh "$REMOTE/nglistutun")"
expect "ENGINE=STOPPED" "$(ctl STATUS)"
expect "PROFILE_REMOVE=OK" "$(mobile "$REMOTE/ngprofiletool remove $PROFILE_ID")"
PROFILE_ID=
echo "DEVICE_PHASE_${PHASE}=OK"
