#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
export DISPLAY=${DISPLAY:-:0}
export XAUTHORITY=${XAUTHORITY:-/home/cat/.Xauthority}
exec ./build/greet_at_desk_demo "$@"
