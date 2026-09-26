#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
EXAMPLES="${LUBANCAT_EXAMPLES:-/root/lubancat_ai_manual_code/example}"
RUNTIME="${RKNN_RUNTIME:-/root/rknn-runtime-test/librknnrt.so}"
if [[ ! -f "$RUNTIME" ]]; then
    RUNTIME="$EXAMPLES/3rdparty/rknpu2/Linux/aarch64/librknnrt.so"
fi
cmake -S "$DEMO_DIR" -B "$DEMO_DIR/build" -DCMAKE_BUILD_TYPE=Release \
    -DLUBANCAT_EXAMPLES="$EXAMPLES" -DRKNN_RUNTIME="$RUNTIME" "$@"
cmake --build "$DEMO_DIR/build" --parallel 2
(cd "$DEMO_DIR/build" && ctest --output-on-failure)
printf 'Built: %s\n' "$DEMO_DIR/build/yolo_camera_demo"
ldd "$DEMO_DIR/build/yolo_camera_demo" | grep -E 'rknn|not found' || true
