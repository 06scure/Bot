# 鲁班猫 2 实时 YOLO26 演示

## 用途与边界

独立 C++17 Demo：V4L2 NV12 采集 → CPU 颜色转换和 letterbox → RKNN NPU 推理 → 野火 YOLO26 后处理 → OpenCV 显示。
源文件位于 `demos/yolo_camera/`。暂不接主程序、ROI、语音或 HTTP；不修改已有单图例程和系统运行库。

已知设备：RK3568，`/dev/video0`，V4L2 Capture Multiplanar，连续 NV12 单内存平面，XFCE 用户 `cat`，X11 `:0`。
默认请求 640×480、30 fps；实际采集尺寸、步长、颜色范围以驱动协商结果为准。30 fps 是请求的相机速率，不是保证的检测帧率。

## 文件职责与 C++ 说明

| 文件 | 职责 |
|---|---|
| `camera.hpp/.cpp` | 查询/设置 V4L2 格式，MMAP 缓冲区、取帧、丢弃旧帧、NV12 转换 |
| `detector.hpp/.cpp` | RKNN 生命周期、模型检查、letterbox、推理、调用野火后处理 |
| `main.cpp` | 参数、单线程循环、画框、统计和退出 |
| `core.hpp`、`core_test.cpp` | 颜色范围转换、补边计算及不依赖硬件的检查 |
| `CMakeLists.txt`、`build.sh` | 独立编译、复制已验证的 Runtime、运行检查 |
| `run.sh` | 为 SSH 启动设置板端 DISPLAY、XAUTHORITY、运行库路径 |

`Camera` 和 `Detector` 把 C 风格的文件描述符、MMAP、RKNN context 包在类中。构造时取得资源，析构时释放（RAII）；异常退出也会清理。禁止复制，避免同一资源被重复释放。
`OutputLease` 确保成功取得的 RKNN 输出在异常时也能归还。信号处理函数只设置 `sig_atomic_t` 标志，主循环负责清理，不在信号函数里调用 OpenCV/RKNN。
相当于把嵌入式 C 中每条错误路径末尾的 `goto cleanup`，集中到确定执行的析构函数中。

## 在板端编译

依赖：CMake、G++、OpenCV C++ 开发包、已经跑通的野火 YOLO26 配套仓库和新 `librknnrt.so`。
不需要 Python、Toolkit2、pip 或联网下载依赖。已有源码被本地引用，不复制整个 SDK 到本项目。

将 `demos/yolo_camera` 上传为 `/root/yolo_camera_demo`，在板端执行：

```bash
cd /root/yolo_camera_demo
bash build.sh
```

默认野火目录为 `/root/lubancat_ai_manual_code/example`，优先使用 `/root/rknn-runtime-test/librknnrt.so`；不存在才使用野火依赖目录的 Runtime。
如果路径不同：

```bash
LUBANCAT_EXAMPLES=/你的路径/example RKNN_RUNTIME=/你的路径/librknnrt.so bash build.sh
```

构建目录为本 Demo 的 `build/`，不会清理野火单图例程的安装目录或模型。
运行库复制到 `build/lib/`，RPATH 为 `$ORIGIN/lib`；`run.sh` 也优先加载此库。`build.sh` 最后打印 `ldd` 结果，请确认不是旧系统 Runtime。
后处理直接编译野火 `postprocess_det.cc`，仅在构建副本中去掉逐帧 `validCount` 打印，避免刷爆 SSH。

## 从 SSH 启动并在板端显示

先关闭 Cheese 等占用摄像头的程序。板端 XFCE 桌面需已登录。

```bash
cd /root/yolo_camera_demo
bash run.sh
```

默认模型：`/root/lubancat_ai_manual_code/example/yolo26/cpp/install/rk356x_linux/model/yolo26n-rk3568-i8.rknn`。
使用 root SSH 会话、`DISPLAY=:0`、`XAUTHORITY=/home/cat/.Xauthority`。不执行 `xhost +`，不修改授权文件权限。
如果桌面用户/显示变化，用 `BOARD_DISPLAY`、`BOARD_XAUTHORITY` 覆盖。自定义模型用 `MODEL_PATH` 或 `--model`。

```bash
# 只画出人（模型计算量不变）
bash run.sh --person-only

# 最大传感器尺寸，CPU 转换更慢
bash run.sh --width 1632 --height 1224

# 运行 100 帧后退出，保存最后一张标注图
bash run.sh --frames 100 --save /root/yolo-last.jpg

# 不开窗口，用于排查采集/推理与显示问题
bash run.sh --headless --frames 100 --save /root/yolo-last.jpg

# 单图对照，确认 Runtime、模型、解码都正确
bash run.sh --headless --image /root/lubancat_ai_manual_code/example/yolo26/model/bus.jpg --save /root/yolo-bus.jpg
```

窗口获得焦点时按 `q`/Esc，或 SSH 中 Ctrl+C 退出。限帧测试自动退出。不要同时启动多份采集程序。

## 数据约定与性能

- 只接受连续 NV12、Multiplanar 接口的一个内存平面；不把 NM12 多内存平面当成 NV12。
- 按驱动实际 stride、bytesused、data_offset 读取，缓冲区长度异常会报错退出。
- 使用驱动报告的 Full/Limited Range 和 BT.601/709 转换；默认颜色属性遵循 V4L2 映射。不支持的色彩矩阵直接拒绝。
- 模型限定为 batch=1、RGB 640×640，三个 NCHW 输出分别为 `[1,84,80,80]`、`[1,84,40,40]`、`[1,84,20,20]`。
- RGB UINT8 交给 Runtime，保留模型内的 `/255` 归一化，不重复归一化。填充值为 114，等比例缩放补边。
- 复用野火 one-to-one 输出后处理，不额外添加 NMS。检测框在回映后再裁剪到原图范围。
- 单线程，不额外排队。每次最多取出驱动现有的 4 个缓冲区，选择最新帧，释放旧帧，再进行推理。
- 每秒打印一次 `capture/color/pre/npu/post/draw_display/loop`，单位毫秒。FPS 是完整循环吞吐量；阶段耗时是该次日志对应帧，不是统计平均值。
- `dropped` 为主动跳过的已积压帧数，不是硬件丢帧计数。
- `age_before_display` 依赖驱动 MONOTONIC 时间戳，是显示提交前的估计帧龄，不含屏幕刷新延迟；`-1` 表示无法可靠计算，不能当作零延迟。
- NV12 转换与 letterbox 在 CPU 上执行，尚未使用 RGA/零拷贝。先测量，再决定优化。

## 故障处理与验收

- `Device busy`：关闭 Cheese/其他采集程序。
- `No camera frame for 3 seconds`：检查 video 节点和 ISP 状态；不要无限循环等待。
- 无法连接桌面：检查 `ps` 中 Xorg 的显示号、当前桌面用户和授权文件；先用 `--headless` 区分显示故障。
- `Invalid RKNN model version`：检查实际加载的 `librknnrt.so`，不要先重新转换模型。
- 模型缺失或初始化失败：明确返回非零，不沿用单图例程的异常清理分支。
- 降低尺寸时颜色/裁剪异常：以协商日志为准，回到已用 Cheese 验证的 1632×1224 作对照。

正式验收应连续运行 10 分钟，观察内存、帧龄、框位置和人物进出；确认退出后可再次启动。
源码构建成功和短时测试不等于完成这项长时间验收。

## 本次板端实测

- 已部署在 `/root/yolo_camera_demo`，GCC 9.4.0、OpenCV 4.2.0；构建及颜色/letterbox 检查通过。
- 实际 Runtime 2.3.2，驱动 0.8.2；`ldd` 指向本 Demo 的 `build/lib/librknnrt.so`。
- `bus.jpg` 识别出公交车和 4 个人，保存图片并检查了框的位置。
- 摄像头接受 640×480 NV12、stride=640、Full Range、BT.601。相机不支持此接口的帧率控制，因此不能宣称相机已设成 30 fps。
- 无窗口 60 帧运行约 10 FPS；板端 X11 显示 300 帧约 9 FPS，正常退出。
- 显示测试后段 NPU 通常约 64–70 ms，颜色转换约 17–19 ms，显示及绘制约 16–17 ms；不是固定性能承诺。
- 帧龄会波动，采样中约 100–200 ms（显示提交前）；积压帧被主动丢弃。
- 兼容了 OpenCV 4.2 GTK 后端对不支持的窗口可见性查询返回 -1 的情况。
- 缺失模型、非法尺寸、缺失摄像头均返回错误码 1，没有段错误。
- 后台短时运行发送 SIGTERM 后返回 0，确认 `/dev/video0` 无残留占用；支持再次启动。
- 测试后已停止 Demo，摄像头可供用户使用。尚未进行 10 分钟稳定性/内存趋势验收，也没有标注数据上的准确率评测。

板端保留 `bus-check.jpg`、`display-check.jpg`、`display-check.log`、`signal-check.log` 供复核，均在 Demo 目录内。
