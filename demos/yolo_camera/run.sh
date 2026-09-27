#!/usr/bin/env bash
set -e
# 固定工作目录，让 config.hpp 中的 model/ 相对路径始终从 Demo 目录开始。
cd -- "$(dirname -- "$0")"

# 在 root SSH 会话启动，显示到 cat 用户已经登录的板端 XFCE 桌面。
export DISPLAY=:0
export XAUTHORITY=/home/cat/.Xauthority

exec ./build/yolo_camera_demo
