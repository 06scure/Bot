#!/usr/bin/env bash
set -e
cd -- "$(dirname -- "$0")"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
