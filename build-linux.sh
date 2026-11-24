#!/bin/sh
# Builds the Linux version (taskbar-vitals) with g++, GTK4 and Cairo. Windows uses build.bat instead.
# Everything Linux-only lives in linux/ and src/linux/; the Windows build never compiles those files.
#   ./build-linux.sh          build ./taskbar-vitals
#   ./build-linux.sh test     run the Linux HID parser tests
#   ./build-linux.sh clean    remove the Linux build output
set -e
cd "$(dirname "$0")"
exec make -f linux/Makefile.linux "$@"
