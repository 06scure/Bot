#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
MODEL="${MODEL_PATH:-/root/lubancat_ai_manual_code/example/yolo26/cpp/install/rk356x_linux/model/yolo26n-rk3568-i8.rknn}"
if [[ ! -x "$DEMO_DIR/build/yolo_camera_demo" ]]; then
    echo 'Build first: bash build.sh' >&2
    exit 1
fi
# Local board display, not the SSH-forwarded display. Override using BOARD_DISPLAY.
export DISPLAY="${BOARD_DISPLAY:-:0}"
export XAUTHORITY="${BOARD_XAUTHORITY:-/home/cat/.Xauthority}"
HEADLESS=false
for arg in "$@"; do [[ "$arg" != --headless ]] || HEADLESS=true; done
if ! $HEADLESS; then
    if [[ ! -r "$XAUTHORITY" ]]; then
        echo "Cannot read desktop authorization: $XAUTHORITY (use desktop user or the existing root session)" >&2
        exit 1
    fi
    if command -v xdpyinfo >/dev/null && ! xdpyinfo >/dev/null 2>&1; then
        echo "Cannot connect to board desktop at $DISPLAY; verify XFCE login and XAUTHORITY" >&2
        exit 1
    fi
fi
# The bundled, known-working Runtime has priority over the old system library.
export LD_LIBRARY_PATH="$DEMO_DIR/build/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$DEMO_DIR/build/yolo_camera_demo" --model "$MODEL" "$@"
