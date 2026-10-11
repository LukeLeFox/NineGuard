#!/bin/sh
# Test host (Linux x86_64) della logica portabile del daemon:
#  1. unit test di NGProfileUAPI/NGUAPIClient/NGNetWatch (ASan/UBSan);
#  2. integrazione con il vero parser UAPI di wireguard-go 0.0.20200320
#     (due device con TUN a canali, handshake e pacchetto consegnato);
#  3. catena completa: profilo generato da ngtestpeer -> builder C ->
#     UAPI -> echo UDP cifrato.
# Non richiede root, non crea interfacce, non tocca route o DNS.
#
# Prerequisiti: gcc, python3 e i binari Go nguapitest, ngclientdev,
# ngtestpeer compilati dal sorgente wireguard-go fissato (vedi README).
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
D=$(CDPATH= cd -- "$HERE/../.." && pwd)
BIN=${NG_HOST_BIN:-$HERE/bin}
T=$(mktemp -d /tmp/nineguard-host-tests.XXXXXX)
trap 'rm -rf "$T"' EXIT
CFLAGS="-std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wno-format-truncation -Wno-unused-result -fsanitize=address,undefined -I$D"

key() { python3 -c "import os,base64;print(base64.b64encode(os.urandom(32)).decode())"; }
K1=$(key); K2=$(key)
H1=$(python3 -c "import base64,sys;print(base64.b64decode(sys.argv[1]).hex())" "$K1")
gcc $CFLAGS -Werror "$HERE/unit.c" "$D/NGProfileUAPI.c" "$D/NGUAPIClient.c" "$D/NGNetWatch.c" -o "$T/unit"
"$T/unit" "$K1" "$H1" "$K2"

if [ -x "$BIN/nguapitest" ]; then
    gcc $CFLAGS -Wno-unused-parameter "$HERE/integ.c" "$D/NGProfileUAPI.c" "$D/NGUAPIClient.c" -o "$T/integ"
    "$BIN/nguapitest" "$T/a.sock" "$T/b.sock" 51842 "$T/info" "$T/go" > "$T/h.log" 2>&1 &
    sleep 0.5
    "$T/integ" "$T/a.sock" "$T/b.sock" 51842 "$T/info" "$T/go"
    wait
    grep HARNESS_DELIVERED "$T/h.log"
fi
if [ -x "$BIN/ngclientdev" ] && [ -x "$BIN/ngtestpeer" ]; then
    gcc $CFLAGS "$HERE/confchain.c" "$D/NGProfileUAPI.c" "$D/NGUAPIClient.c" -o "$T/confchain"
    "$BIN/ngtestpeer" -listen-port 51851 -endpoint-host 127.0.0.1 \
        -client-conf "$T/client.conf" -duration 12s > "$T/peer.log" 2>&1 &
    "$BIN/ngclientdev" "$T/c.sock" "$T/go2" > "$T/cd.log" 2>&1 &
    sleep 0.3
    "$T/confchain" "$T/client.conf" "$T/c.sock" "$T/go2"
    wait
    cat "$T/cd.log"; grep UDP_ECHO "$T/peer.log" | tail -1
fi
echo "HOST_TESTS=DONE"
