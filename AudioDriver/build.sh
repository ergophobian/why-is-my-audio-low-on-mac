#!/bin/bash
# Build the AudioBoost HAL virtual audio driver
# Produces a .driver bundle ready for installation to /Library/Audio/Plug-Ins/HAL/
set -e

DRIVER_NAME="AudioBoostDriver"
BUILD_DIR="build"
DRIVER_BUNDLE="$BUILD_DIR/$DRIVER_NAME.driver"

echo "=== Building $DRIVER_NAME ==="

# Clean previous build
rm -rf "$DRIVER_BUNDLE"
mkdir -p "$DRIVER_BUNDLE/Contents/MacOS"

# Compile as a dynamic library (the .driver bundle's executable)
clang -dynamiclib \
    -o "$DRIVER_BUNDLE/Contents/MacOS/$DRIVER_NAME" \
    AudioBoostDriver.c RingBuffer.c \
    -framework CoreAudio \
    -framework CoreFoundation \
    -install_name "/Library/Audio/Plug-Ins/HAL/$DRIVER_NAME.driver/Contents/MacOS/$DRIVER_NAME" \
    -arch arm64 -arch x86_64 \
    -mmacosx-version-min=13.0 \
    -std=c99 \
    -Wall -Wextra -Wpedantic \
    -O2

# Copy the Info.plist into the bundle
cp Info.plist "$DRIVER_BUNDLE/Contents/"

echo ""
echo "=== Build successful: $DRIVER_BUNDLE ==="
echo ""
echo "To install (requires sudo):"
echo "  sudo cp -R $DRIVER_BUNDLE /Library/Audio/Plug-Ins/HAL/"
echo "  sudo chown -R root:wheel /Library/Audio/Plug-Ins/HAL/$DRIVER_NAME.driver"
echo "  sudo launchctl kickstart -k system/com.apple.audio.coreaudiod"
echo ""
echo "To uninstall:"
echo "  sudo rm -rf /Library/Audio/Plug-Ins/HAL/$DRIVER_NAME.driver"
echo "  sudo launchctl kickstart -k system/com.apple.audio.coreaudiod"
