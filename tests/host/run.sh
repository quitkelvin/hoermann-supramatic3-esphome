#!/bin/sh
# Host test of the command/state logic (no ESP32, no ESPHome needed): sh tests/host/run.sh
set -e
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/uapbridge_host_test"
g++ -std=gnu++17 -Wall -Wno-unused-variable -Wno-unused-function -I stubs \
  test_uapbridge.cpp ../../components/uapbridge/uapbridge.cpp ../../components/uapbridge_esp/uapbridge_esp.cpp \
  -o "$OUT"
"$OUT"
