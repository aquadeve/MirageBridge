#!/usr/bin/env bash
set -e

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ANDROID_DIR="$ROOT_DIR/android"

if [ -z "$ANDROID_SDK_ROOT" ]; then
  echo "ANDROID_SDK_ROOT is required"
  exit 1
fi

cd "$ANDROID_DIR"
if [ -x ./gradlew ]; then
  ./gradlew assembleDebug
else
  gradle assembleDebug
fi
