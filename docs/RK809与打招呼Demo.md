# RK809、MeloTTS 与到岗问候验证记录

日期：2026-10-09。设备：LubanCat，192.168.100.5，RK3568，aarch64，Linux 4.19.232，GCC 9.4。

## 交付内容

1. [独立音频 demo](../demos/rk809_tts/README.md)：RK809 SPK 提示音、用户权重的中文 RKNN 推理、WAV 输出和 ALSA 播放。
2. [到岗问候 demo](../demos/greet_at_desk/README.md)：复用已有摄像头/RGA/YOLO26，加入到岗状态机、进程内音频缓存和独立播放线程。
3. [状态机实现](../src/trigger/desk_state_machine.cpp)：从原来的抛出“未实现”改为实际连续帧、ROI、离岗与冷却规则。
4. [模型来源及校验清单](../demos/rk809_tts/model/manifest.json)：记录用户原始权重及每份导出文件的 SHA-256。

板端安装位置是 `/root/bot_demos/demos/`。其中 `yolo_camera` 是指向已有 `/root/yolo_camera_demo` 的链接，复用视觉源码、模型与 Runtime，没有修改原视觉 demo。源码在主机编辑后传输，所有 C++ 编译在板端完成。

顶层 `src/main.cpp` 仍是原有工程骨架；本次入口是两个独立 demo，不要用顶层 `bot` 可执行文件启动这些功能。

## 已完成的验证

| 项目 | 实测结果 |
|---|---|
| RK809 | 内核已注册 `rockchiprk809co`，本机 card 0；SPK 路由可用 |
| 提示音 | 44.1 kHz、16 位、单声道、440 Hz、2 秒；ALSA 返回成功 |
| 权重加载 | 根目录 `checkpoint.pth`，PyTorch strict 加载成功 |
| 转换 | ONNX 检查/简化通过；Toolkit2 2.3.2，target=rk3568，FP16，两个 RKNN 均生成并实板运行 |
| 中文问候 | “早上好，欢迎回来。”，94720 个采样点，2.14785 秒 |
| WAV 检查 | 44.1 kHz，16 位单声道；峰值约 0.55417，RMS 0.08768，削波采样数 0 |
| 首次启动及合成 | 13440.4 ms（含模型/词典加载、合成和写 WAV；不含播放） |
| 分段耗时 | 词典 2314.48 ms，encoder 447.525 ms，中间展开 0.323 ms，decoder 10147.7 ms |
| 状态机 | 32 项断言通过，包含五帧确认、候选中断、短暂漏检、离岗阈值、冷却保持、边界、非法值与时间倒退 |
| 事件回放 | 合成检测数据驱动 3 次到岗，2 次实际播放成功，冷却内第 2 次到岗被抑制 |
| 无窗口摄像头测试 | 60 秒，691 帧，正常退出；无人场景，0 次到岗、0 次播放 |
| 帧率 | 57 个约一秒窗口的平均值约 11.52 FPS，范围 10.15–13.73 FPS |
| 显示窗口测试 | 20 秒，208 帧，正常退出；无人场景 |
| 错误路径 | 缺模型明确返回失败；词典缺词 `☃` 明确报错，不生成替代语音 |

回放中的 `queue_ms` 实测 0.268/0.159 ms，**仅表示从入队到播放线程开始调用**，不代表声卡开始发声延迟。未测扬声器实际声学输出的首音时间。耗时是本次短测数据，不能当作长期性能保证。

证据目录：[validation/rk809_tts](validation/rk809_tts)。其中：

- `tts-test.log`：独立中文合成/播放。
- `integration-test.log`：真实摄像头一分钟运行。
- `replay-test.log`：明确标记为 SYNTHETIC_DETECTION_REPLAY 的事件回放。
- `gui-test.log`：显示窗口运行。
- `build.log`：闭环构建和 CTest。
- `missing-model-test.log`、`unknown-token-test.log`：失败路径。

**仍需人工验收**：远程日志无法证明喇叭的实际音量、清晰度和完整发音；需要设备旁听检。本次真实摄像头画面没有检测到人，因此真人进入→自动问候的现场验收未完成。不能把事件回放称为真人验收。未执行一小时稳定性测试，也未实现阶段计划中的 HTTP/systemd 完整服务。

测试程序均已正常退出，没有设置自启动。音频 mixer 保留测试使用的 SPK/70% 状态，没有执行 `alsactl store`。

## C++ 实现讲解（对应 C/Python 经验）

### 1. 用对象生命周期管理硬件句柄

`tts.cpp` 的 `Model` 在构造时调用 `rknn_init`，析构时调用 `rknn_destroy`。你在 C 中通常用 `goto cleanup` 统一释放；C++ 用这种 RAII 写法，让正常返回和抛异常都自动释放。构造中途失败也有显式清理，避免一个模型加载成功、下一个失败时漏掉已申请资源。

`Model(const Model&)=delete` 禁止复制硬件句柄：两个对象如果持有同一 RKNN 句柄，析构时可能重复销毁。`MeloTts` 用 `std::unique_ptr<Impl>` 独占实现对象；头文件只暴露“合成文本”接口，不把 RKNN 类型扩散到调用者。

### 2. vector 替代手动管理的数组

文本前端产生音素、声调、语言数组，使用 `std::vector<int64_t>`。与 Python list 不同，vector 的元素类型固定且内存连续，可通过 `.data()` 传给 C 风格 RKNN API。缓冲区由 vector 持有，RKNN 调用结束之前一直有效。

RKNN 返回的输出另有所有权：代码先复制到 vector，再调用 `rknn_outputs_release`。这样后续处理中不会引用已释放的 Runtime 内存。

### 3. 时长展开仍是 CPU 工作

encoder 为每个音素给出时长参数。代码计算 `ceil(exp(logw) * mask)`，按累计位置构建 `attn[声学帧][音素]`，再交给 decoder 生成波形。这个环节没有再次调用 Python。长度超过 512 个声学帧会报错，避免上游示例直接截断尾音。

新实现逐一编号模型输入，修正了参考示例 decoder 将 `g` 输入索引误写为 1 的问题（正确为 2）；加载时检查输入/输出数量、名称和元素数，整数输入还检查 INT64 类型。

### 4. 到岗事实与“可以问候”分开

`std::optional<DeskEvent>` 相当于 Python 的“一个事件或 None”，比“用某个枚举值代表没有事件”更明确。`ArrivalConfirmed` 是到岗事实，`greeting_allowed` 才代表这次是否能问候，因此冷却期间可以记录到岗而不播放。

`steady_clock` 是单调时钟，不受系统时间校准影响。测试通过传入虚拟时间推进状态，不需要真的等待 10 分钟。

### 5. 播放线程只处理已接受的一条请求

`AudioWorker` 由应用层创建，内部使用 `std::mutex`、`std::condition_variable` 和 `std::thread`。可以理解为 Python 的 Lock、Condition、Thread。视觉线程只短暂加锁入队，实际播放不持有锁，不阻塞后续摄像头推理。

`ready_.wait(lock, predicate)` 会在等待时释放锁，并在唤醒后重新检查条件，以正确处理虚假唤醒。忙时拒绝请求，避免人员频繁进出造成问候积压。析构时通知线程并 `join()`，等待已接受的播放完成；主线程检查播放失败标志后再报告退出成功。

信号处理器只写 `sig_atomic_t` 标志，资源释放、线程等待与日志都留在正常执行路径中，避免在信号处理器内调用不安全操作。

### 6. 本阶段缓存的含义

闭环每次启动根据当前文本、当前模型和词典重新合成一次，写入 `mkdtemp` 创建的私有目录。之后的到岗请求只读同一 WAV。由于不跨进程复用缓存，修改模型或文本不会错误命中旧文件；退出时删除临时文件。这是固定问候 demo 的缓存策略，还不是阶段计划中支持任意文本的持久化缓存服务。
