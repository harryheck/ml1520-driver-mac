#!/bin/bash
# Builds build/ml1520-print. libusb is linked statically, so the program needs neither
# Homebrew nor CUPS at runtime. Requires the Xcode Command Line Tools and "brew install libusb".
set -euo pipefail
cd "$(dirname "$0")"

SPLIX=third_party/splix
USB_PREFIX=$(brew --prefix libusb)

mkdir -p build
clang++ -arch arm64 -std=gnu++11 -O2 -g -Wall \
    -I"$SPLIX/include" -I"$USB_PREFIX/include/libusb-1.0" \
    driver/ml1520-print.cpp \
    "$SPLIX/src/algo0x11.cpp" "$SPLIX/src/algorithm.cpp" "$SPLIX/src/bandplane.cpp" \
    "$USB_PREFIX/lib/libusb-1.0.a" \
    -framework IOKit -framework CoreFoundation -framework Security \
    -o build/ml1520-print
codesign --force --sign - build/ml1520-print
