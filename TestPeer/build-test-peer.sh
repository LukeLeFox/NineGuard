#!/bin/sh
# Compila ngtestpeer per il PC Linux usando lo stesso commit wireguard-go
# dell'engine iOS e lo stesso Go 1.14.15 isolato. Nessun pacchetto di
# sistema, nessun root.
set -eu

COMMIT=52c834c4469f4f6e33c97c7294faffabfcbf114a
ARCHIVE_SHA256=d2d6040070044e7f92a0d8d6d2a32a17642587f1c2959909acfdceb25f8b9a97
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
GO_ROOT=${NINEGUARD_GO_ROOT:-${HOME}/.local/opt/nineguard-go114/go}
CACHED=${NINEGUARD_WG_ARCHIVE:-/tmp/nineguard-wireguard-go-2020/source.tar.gz}
BUILD_ROOT=$(mktemp -d /tmp/nineguard-testpeer-build.XXXXXX)
ARCHIVE="$BUILD_ROOT/source.tar.gz"
SOURCE_DIR="$BUILD_ROOT/wireguard-go-$COMMIT"
OUTPUT_DIR="$SCRIPT_DIR/build"

cleanup() { rm -rf "$BUILD_ROOT"; }
trap cleanup EXIT HUP INT TERM

[ -x "$GO_ROOT/bin/go" ] || { echo "STOP: Go mancante in $GO_ROOT" >&2; exit 2; }

if [ -f "$CACHED" ]; then
    cp "$CACHED" "$ARCHIVE"
else
    curl -fL --retry 3 --connect-timeout 10 \
        "https://github.com/WireGuard/wireguard-go/archive/$COMMIT.tar.gz" \
        -o "$ARCHIVE"
fi
printf '%s  %s\n' "$ARCHIVE_SHA256" "$ARCHIVE" | sha256sum -c -
tar -xzf "$ARCHIVE" -C "$BUILD_ROOT"
mkdir -p "$SOURCE_DIR/cmd/ngtestpeer" "$OUTPUT_DIR"
cp "$SCRIPT_DIR/ngtestpeer.go" "$SOURCE_DIR/cmd/ngtestpeer/main.go"
cd "$SOURCE_DIR"
env GO111MODULE=on GOOS=linux GOARCH=amd64 CGO_ENABLED=0 \
    "$GO_ROOT/bin/go" build -trimpath -o "$OUTPUT_DIR/ngtestpeer" ./cmd/ngtestpeer
"$OUTPUT_DIR/ngtestpeer" -selftest -listen-port 51899 \
    -client-conf "$BUILD_ROOT/selftest.conf"
echo "TESTPEER_BUILD=OK"
