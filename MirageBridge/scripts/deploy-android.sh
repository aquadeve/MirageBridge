#!/usr/bin/env bash
set -e

if [ $# -lt 1 ]; then
  echo "usage: $0 <device-serial-optional>"
fi

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
APK="$ROOT_DIR/android/app/build/outputs/apk/debug/app-debug.apk"

adb ${1:+-s $1} install -r "$APK"
adb ${1:+-s $1} shell am start -n com.miragebridge/.MainActivity
