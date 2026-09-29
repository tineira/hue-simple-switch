#!/usr/bin/env bash
# Host tests for the JSON and config-poll parsers: plain C++ built with the PC's compiler, no
# board. CI (build.yml) runs this with SANITIZE=1. Locally: test/host/run.sh
# (CXX picks the compiler, default g++).
set -euo pipefail
cd "$(dirname "$0")/../.."

CXX="${CXX:-g++}"
FLAGS=(-std=gnu++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function)
if [ "${SANITIZE:-0}" = "1" ]; then
  FLAGS+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
fi
OUT_DIR="${OUT_DIR:-build/host-tests}"
mkdir -p "$OUT_DIR"

# Stubs first, so <Arduino.h> and friends resolve to test/host/stubs.
$CXX "${FLAGS[@]}" -I test/host/stubs -I . test/host/test_parsers.cpp -o "$OUT_DIR/test_parsers"
"$OUT_DIR/test_parsers"
