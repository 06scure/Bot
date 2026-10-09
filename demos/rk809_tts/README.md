# RK809 喇叭与 MeloTTS demo

本 demo 使用用户提供的根目录 `checkpoint.pth`，经 ONNX → RK3568 FP16 RKNN 转换后，在板端执行 **C++ 文本前端 → encoder → 时长展开 → decoder → WAV → ALSA**。不调用云端 TTS，不播放预置语音代替合成。

实板 RK809 已由内核驱动注册，无需改设备树或重新写 codec 驱动。2026-10-09 实测 RK809 是 **card 0**，HDMI 是 card 1；程序使用 `rockchiprk809co` 名称，避免依赖编号。

## 板端使用

已部署目录：`/root/bot_demos/demos/rk809_tts`。

```bash
cd /root/bot_demos/demos/rk809_tts
bash build.sh
bash run.sh tone
bash run.sh tts
bash run.sh tts '你好，欢迎回来。'
```

启动会设置 `Playback Path=SPK`、`Playback=70%`。提示音是 2 秒、440 Hz、幅度 0.12 的正弦波，带渐入渐出。TTS 默认说“早上好，欢迎回来。”，生成 `greeting.wav` 后播放。退出码非零表示失败，不能仅凭文件存在判定成功。

音量由 `audio.cpp` 中 `configure_speaker()` 设定，修改后重新编译。70% 对应当前板卡约 -29.51 dB，百分比不等于声压。程序不会保存到系统启动配置。需要单独重播时：

```bash
aplay -D plughw:CARD=rockchiprk809co,DEV=0 greeting.wav
```

`plughw` 允许 ALSA 按设备能力转换采样格式。板端有 `aplay`、`amixer`，缺少 ALSA 开发头文件，因此 demo 通过 `posix_spawnp` 直接调用 ALSA 工具；不经过 shell，不需要补装开发包。这是用户态播放链路，不是新内核驱动。

## 构建依赖和模型

- 板端 GCC 9.4、CMake 3.16+、pthread，以及已有 `../yolo_camera/3rdparty/rknpu2` 头文件和 aarch64 Runtime。
- 可通过 `bash build.sh -DYOLO_DIR=/path/to/yolo_camera` 指定依赖位置。
- `model/encoder.rknn`、`decoder.rknn`、`lexicon.txt`、`tokens.txt` 必须成套提供。
- ONNX/RKNN 大文件被 `.gitignore` 忽略，但本次已经生成在本地目录并传到板卡。复制/部署时要包含两份 `.rknn`，仅克隆 Git 不会得到模型。
- `model/manifest.json` 保存原始权重、配置、词典和导出模型的 SHA-256，以及转换版本。

词典来源：野火配套仓库 `642dbd4bc3cb1b571a40731f0d2a87c3fef7807a` 的 `example/melotts/model`。模型配置来源：MyShell 官方 MeloTTS-Chinese。导出参考 [野火 MeloTTS 教程](https://doc.embedfire.com/linux/rk356x/Ai/zh/latest/lubancat_ai/example/tts.html#melotts)，导出源码固定为 `mmontol/MeloTTS` 的 `711e65f8253df7138c53261aa6aad04ae539c282`。

主机 Linux/WSL 环境中的复现命令（从项目根目录执行，板端不联网）：

```bash
# 环境版本：torch 2.4.0+cpu / onnx 1.17.0 / RKNN Toolkit2 2.3.2
# 补充依赖：numba 0.60.0 / onnxsim 0.4.36
git clone https://github.com/mmontol/MeloTTS.git tmp/melotts-export
git -C tmp/melotts-export checkout 711e65f8253df7138c53261aa6aad04ae539c282
python demos/rk809_tts/tools/export_models.py \
  --source tmp/melotts-export --checkpoint checkpoint.pth \
  --config demos/rk809_tts/model/config.json --output demos/rk809_tts/model
python demos/rk809_tts/tools/convert_models.py demos/rk809_tts/model
```

本次实际导出脚本是 `tools/export_models.py`；转换使用野火配套 `convert.py`，参数为 `rk3568 fp`。新增 `tools/convert_models.py` 封装相同的 config/load/build/export 调用，方便后续复现；未为验证封装而重复编译同一模型。

## 边界

- 固定中文/英文词典，未启用 BERT；未知词直接报错，不静默漏读。
- 语速 1.0，中文 speaker ID 0，语言 ID 3。当前实现对应这份中文模型，不是任意语言模型加载器。
- 最多 256 音素位置（含空白），最长 512 个声学帧，约 5.94 秒；超长报错，不截断。
- 不支持长文章自动分句。用短句测试，开放词汇或长文本是后续工作。
- 每次独立 demo 启动会加载模型并合成，实测冷启动约 13.4 秒；闭环 demo 只在启动时合成一次。
- 播放进程超时上限 20 秒，异常回收子进程并报错。

测试数据和 C++ 说明见 [验证报告](../../docs/RK809与打招呼Demo.md)。
