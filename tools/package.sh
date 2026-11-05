#!/bin/bash
# Creates the release files in dist/: the ready-to-use zip, and the exact libusb source the
# program was linked against, which the GPL requires alongside the binary.
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION=1.0.0
NAME="ml1520-driver-mac-$VERSION-arm64"
LIBUSB_VERSION=1.0.30
LIBUSB_SHA256=fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf
LIBUSB_TARBALL="libusb-$LIBUSB_VERSION.tar.bz2"

[ "$(brew list --versions libusb)" = "libusb $LIBUSB_VERSION" ] || {
    echo "Expected libusb $LIBUSB_VERSION, update third_party/libusb and this script." >&2; exit 1; }

./build.sh
tests/run.sh

rm -rf dist
mkdir -p "dist/$NAME/build" "dist/$NAME/driver"
cp build/ml1520-print "dist/$NAME/build/"
cp driver/ml1520.conf driver/ml1520-print.cpp "dist/$NAME/driver/"
cp build.sh install.sh uninstall.sh README.md LICENSE "dist/$NAME/"
cp -R third_party "dist/$NAME/"
ditto -c -k --norsrc --noextattr --keepParent "dist/$NAME" "dist/$NAME.zip"
rm -rf "dist/$NAME"

curl -fsSL -o "dist/$LIBUSB_TARBALL" \
    "https://github.com/libusb/libusb/releases/download/v$LIBUSB_VERSION/$LIBUSB_TARBALL"
echo "$LIBUSB_SHA256  dist/$LIBUSB_TARBALL" | shasum -a 256 -c -

ls -la dist
