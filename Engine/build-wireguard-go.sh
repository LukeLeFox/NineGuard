#!/bin/sh
set -eu

COMMIT=52c834c4469f4f6e33c97c7294faffabfcbf114a
ARCHIVE_SHA256=d2d6040070044e7f92a0d8d6d2a32a17642587f1c2959909acfdceb25f8b9a97
GO_VERSION=go1.14.15

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
THEOS_ROOT=${THEOS:-/home/luke/theos}
GO_ROOT=${NINEGUARD_GO_ROOT:-/home/luke/.local/opt/nineguard-go114/go}
BUILD_ROOT=$(mktemp -d /tmp/nineguard-engine-build.XXXXXX)
ARCHIVE="$BUILD_ROOT/source.tar.gz"
SOURCE_DIR="$BUILD_ROOT/wireguard-go-$COMMIT"
OUTPUT_DIR="$SCRIPT_DIR/build"

cleanup() {
    rm -rf "$BUILD_ROOT"
}
trap cleanup EXIT HUP INT TERM

if [ ! -x "$GO_ROOT/bin/go" ]; then
    echo "STOP: Go 1.14 toolchain missing: $GO_ROOT/bin/go" >&2
    exit 2
fi
if [ ! -x "$THEOS_ROOT/toolchain/linux/iphone/bin/clang" ] ||
   [ ! -d "$THEOS_ROOT/sdks/iPhoneOS9.2.sdk" ] ||
   [ ! -x "$THEOS_ROOT/toolchain/linux/iphone/bin/ldid" ]; then
    echo "STOP: Theos iOS 9.2 toolchain incomplete" >&2
    exit 2
fi
if [ ! -x "$SCRIPT_DIR/clang-ios9" ]; then
    echo "STOP: clang-ios9 wrapper is not executable" >&2
    exit 2
fi
if [ "$($GO_ROOT/bin/go version | awk '{print $3}')" != "$GO_VERSION" ]; then
    echo "STOP: expected $GO_VERSION" >&2
    exit 2
fi

curl -fL --retry 3 --connect-timeout 10 \
    "https://github.com/WireGuard/wireguard-go/archive/$COMMIT.tar.gz" \
    -o "$ARCHIVE"
printf '%s  %s\n' "$ARCHIVE_SHA256" "$ARCHIVE" | sha256sum -c -
tar -xzf "$ARCHIVE" -C "$BUILD_ROOT"

mkdir -p "$OUTPUT_DIR"
cd "$SOURCE_DIR"
env GO111MODULE=on GOOS=darwin GOARCH=arm GOARM=7 CGO_ENABLED=1 \
    CC="$SCRIPT_DIR/clang-ios9" \
    "$GO_ROOT/bin/go" build -trimpath -ldflags='-s -w' \
    -o "$OUTPUT_DIR/wireguard-go" .
"$THEOS_ROOT/toolchain/linux/iphone/bin/ldid" \
    -S "$OUTPUT_DIR/wireguard-go"
cp "$SOURCE_DIR/COPYING" "$OUTPUT_DIR/COPYING.wireguard-go"

echo "ENGINE_BUILD=OK"
file "$OUTPUT_DIR/wireguard-go"
sha256sum "$OUTPUT_DIR/wireguard-go"
