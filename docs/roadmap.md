# Roadmap

> 本文是项目路线图，和 `README.md` 的“当前状态”配合使用。
> 原则：每个版本都必须能演示；没有达到验收标准，不打版本标签。

---

## 1. 当前阶段

```text
W0 工程底座        ✅ 基本完成
W1 最小闭环        🟡 完成大半：LCD 能画、YUYV 能转，但摄像头帧还没上屏
v0.1               ❌ 尚未完成
v0.5 / v1.0        ❌ 未开始
```

当前状态一句话：

> **V4L2 采集、配置、日志、LCD、YUYV 转换都已经分段跑通，下一步是把摄像头 YUYV 帧直接送到 LCD。**

当前适合的标签：

```text
lcd-yuyv-milestone
```

或：

```text
v0.1.0-alpha
```

**不要打 `v1.0`。**

---

## 2. 版本定义

### v0.1 最小闭环

目标：

- 抓 1 帧 YUYV；
- 转 RGB565；
- 写满 LCD；
- 屏幕上出现摄像头画面。

验收：

- [ ] `test_camera_preview` 能显示一帧真实摄像头画面；
- [ ] 正确处理 `bytesperline`；
- [ ] 摄像头分辨率 ≤ LCD 分辨率时 1:1 显示；
- [ ] 程序正常退出，无段错误；
- [ ] 有日志打印实际格式、分辨率、stride。

预计：1–2 天。

### v0.5 连续预览

目标：

- 连续采集；
- 双缓冲；
- FPS 统计；
- 10 分钟不崩。

验收：

- [ ] 连续运行 10 分钟；
- [ ] FPS 稳定；
- [ ] 无内存增长；
- [ ] Ctrl-C 能干净退出；
- [ ] 日志能看到 FPS/耗时。

预计：1 周。

### v1.0 可用

目标：

- 多分辨率/多格式；
- letterbox；
- 触摸/按键拍照；
- JPEG 存卡；
- OSD 显示 FPS/分辨率；
- 连续 8 小时稳定。

验收：

- [ ] 3 种分辨率切换不花屏；
- [ ] 触摸能拍照；
- [ ] PC 能打开照片；
- [ ] OSD 正常；
- [ ] 8 小时不崩。

预计：3–4 周。

### v1.5 联网

- MJPEG over HTTP；
- 手机浏览器查看；
- 多客户端；
- 延迟 <300ms。

### v2.0 可写简历

- 分层重构；
- 单元测试；
- 设计文档；
- Buildroot 精简镜像；
- 开机自启；
- 看门狗。

---

## 3. 对照 12 周计划

原计划来自：

```text
D:\Desktop\deepseek_harness_project\嵌入式Linux学习规划-相机主线.md
```

当前对照：

| 周 | 计划 | 当前状态 |
|---|---|---|
| W0 | CMake、log、ini、gdbserver、Git | ✅ 基本完成 |
| W1 | 一帧 YUYV → RGB565 → fb0 | 🟡 LCD/YUYV 已通，摄像头帧未上屏 |
| W2 | 连续采集、FPS | ❌ 未开始 |
| W3 | 查表、letterbox、多分辨率 | ❌ 未开始 |
| W4 | 触摸/UI/OSD | ❌ 未开始 |
| W5 | libjpeg、存卡 | 🟡 MJPEG 直接保存已通，完整存卡未做 |
| W6 | HTTP MJPEG | ❌ 未开始 |
| W7 | 重构/测试/文档 | 🟡 目录已整理，测试/文档未完成 |
| W8–W9 | I2C/Input 驱动 | ❌ 未开始 |
| W10 | V4L2 深入 | 🟡 枚举/格式有一定实践 |
| W11 | Buildroot/自启/看门狗 | ❌ 未开始 |
| W12 | 性能/README/演示 | 🟡 README 已有，性能和演示未做 |

结论：

> **当前处于 W0 完成、W1 进行中。主线没有偏，但 v0.1 的“摄像头画面”必须尽快完成。**

---

## 4. 接下来两周

### 第 1 天：收尾当前阶段

- [ ] 更新 README（当前状态、目录、运行方式）；
- [ ] 提交当前可用版本；
- [ ] 打 `lcd-yuyv-milestone` 标签；
- [ ] 记录 LCD/YUYV 调试经验到 `docs/debug-log.md`。

### 第 2 天：整理模块

- [ ] 把 `lcd.c/h` 中不合理的全局状态/生命周期问题记下来，不急着大改；
- [ ] 给 `yuyv_to_rgb()` 增加 `src_stride` 参数；
- [ ] 把 `generate_yuyv_region()` 移到 `tests/manual/` 或测试文件；
- [ ] 让 `include/lcd.h`、`include/yuyv.h` 自包含（include `config.h` 或前向声明）。

### 第 3–4 天：单帧摄像头预览

- [ ] 新建/实现 `tests/manual/test_camera_preview.c`；
- [ ] `S_FMT` 设置为 YUYV；
- [ ] 读回实际 `width/height/bytesperline/pixelformat`；
- [ ] `REQBUFS`、`mmap`、`QBUF`、`STREAMON`；
- [ ] `poll` + `DQBUF` 一帧；
- [ ] `yuyv_to_rgb()`；
- [ ] `lcd_put_pixel()` 显示；
- [ ] `QBUF`、`STREAMOFF`、`munmap`、`close`。

验收：

- [ ] 屏幕上出现真实摄像头画面；
- [ ] 颜色基本正确；
- [ ] 无段错误；
- [ ] 日志打印实际参数。

### 第 5–7 天：连续预览

- [ ] 循环 DQBUF/QBUF；
- [ ] FPS 统计；
- [ ] `poll` 超时；
- [ ] Ctrl-C 退出；
- [ ] 连续 10 分钟不崩。

### 第 2 周：v0.5 双缓冲

- [ ] 双缓冲；
- [ ] `FBIOPAN_DISPLAY`；
- [ ] letterbox 预留；
- [ ] 性能日志；
- [ ] 代码从测试程序抽到 `src/camera.c` / `src/display.c`。

---

## 5. 风险

| 风险 | 影响 | 对策 |
|---|---|---|
| `bytesperline` 与 `width*2` 不一致 | 画面斜/花 | 先打印 stride，转换函数加参数 |
| 摄像头分辨率 > LCD | 越界/崩溃 | 先选 ≤ LCD 的分辨率 |
| `lcd_put_pixel` 太慢 | FPS 低 | 单帧先验证，再直接写 RGB565 行 |
| 全局 `full` 状态 | 忘记调用导致转换失败 | 改成参数或显式初始化 |
| 配置结构膨胀 | 维护困难 | v0.5 后再拆 |
| 继续加测试程序不抽模块 | 目录失控 | 先完成 v0.1，再重构 |
| 提前做触摸/OSD/网络 | 主线延迟 | 先出画面 |

---

## 6. 版本标签规则

- 只有达到版本验收，才打版本标签；
- 当前最多打：
  - `lcd-yuyv-milestone`
  - `v0.1.0-alpha`
- 不要在 v0.1 之前打 `v1.0`；
- 每个标签附：
  - 能演示什么；
  - 不能做什么；
  - 已知问题；
  - 构建/运行命令。

---

## 7. 一句话

> **先让摄像头画面出现在 LCD 上（v0.1），再谈连续、双缓冲和性能。**
