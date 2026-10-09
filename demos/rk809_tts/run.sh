#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
exec ./build/rk809_tts_demo "${@:-tts}"
