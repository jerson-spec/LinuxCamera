# Roadmap

> 本文是项目路线图，和 `README.md` 的“当前状态”配合使用。
> 原则：每个版本都必须能演示；没有达到验收标准，不打版本标签。

---

## 1. 当前阶段

```text
W0 工程底座        ✅ 完成
W1 最小闭环        ✅ 完成（摄像头 YUYV 单帧 → LCD）
v0.1               ✅ 完成并打标签
W2 连续 + FPS      ✅ 完成（604 s / 9036 帧 / 0 丢帧）
v0.5 连续预览      ✅ 完成并打标签
W3 性能/适配       ✅ 完成（conv 14.0 / draw 8.6 / CPU 34%；640×480@30）
W4 触摸/UI/OSD     ✅ 完成
W5 拍照存卡        ✅ 完成（MJPEG 直存 jpg）
v1.0               ✅ 完成（8 h / 610390 帧 / 0 错误）
W6 网络推流        ✅ 完成（/snapshot + /stream + 首页）
v1.5               ✅ 完成并打标签（v1.5）
W8–W9 驱动         ✅ 基本完成（hello_misc、button_plat、ap3216c + 自动亮度）
W11 Buildroot      ❌ 之后
```

当前状态一句话：

> **v1.5 已经能演示：LCD 预览、触摸/按键拍照、切分辨率、浏览器看 MJPEG 流；
> 驱动做完了字符设备、poll、platform + input、I2C 光感，屏幕能跟着环境光自己调亮度。**

当前功能对应：

```text
v1.5 + W8–W9（基本完成）
```

---

## 2. 当前已知事实

- 摄像头请求 1024×600，实际返回 **800×600 YUYV**；
- `bytesperline = 1600 = 800 × 2`；
- LCD 为 **1024×600，32bpp**；
- 画面用 letterbox 居中，800×600 时左右各 112 像素黑色；
- 黑边是分辨率/宽高比不匹配，不是 bug；
- 摄像头在 800×600 YUYV 下只有 15/10/5 fps 三档，实测 15.0 fps；
- 640×480 下支持 30/25/20/15/10/5，实测 30.0 fps（`VIDIOC_S_PARM` 请求）；
- 连续预览用 4 buffer + `poll` 超时 + Ctrl-C 退出，双缓冲走 `FBIOPAN_DISPLAY`。

---

## 3. 版本定义

### v0.1 最小闭环（已完成）

目标：

- 抓 1 帧 YUYV；
- 转 RGB；
- 写 LCD；
- 屏幕出现摄像头画面。

验收：

- [x] 摄像头 YUYV 采集；
- [x] YUYV → RGB；
- [x] LCD 单帧显示；
- [x] 实际 800×600，1:1 左上角显示；
- [x] 程序正常退出，无段错误；
- [x] 提交并打标签（`v0.1`）。

### v0.5 连续预览（已完成）

目标：

- 连续采集；
- 双缓冲；
- FPS 统计；
- 10 分钟不崩。

验收：

- [x] 连续运行 10 分钟（604 s / 9036 帧）；
- [x] FPS 稳定并可打印（15.0）；
- [x] 无内存增长（VmRSS 6324 kB 恒定）；
- [x] Ctrl-C 干净退出；
- [x] 日志能看到 wait/conv/draw 耗时；
- [x] 采集不因 LCD 慢而卡死（提前 QBUF，0 丢帧）。

### v1.0 可用

目标：

- 多分辨率/多格式；[x]（配置驱动，800×600 / 640×480 / 352×288 已验证）
- letterbox；[x]
- 触摸/按键拍照；[x]
- JPEG 存卡；[x]
- OSD 显示 FPS/分辨率；[x]
- 连续 8 小时稳定。[x]

验收：

- [x] 3 种分辨率切换不花屏（8h 内 15 次）；
- [x] 触摸能拍照（左半边）；
- [x] PC 能打开照片（47 张全部可打开）；
- [x] OSD 正常；
- [x] 8 小时不崩（610390 帧、0 错误、RSS/fd 不增长）。

### v1.5 联网（已完成）

- [x] MJPEG over HTTP（`/stream`）；
- [x] 单张 JPEG（`/snapshot.jpg`）和首页（`/`）；
- [x] 空连接处理（300ms 超时，不再 +50 掉帧）；
- [x] 手机/浏览器查看（Android Chrome；iOS Safari 不支持流）；
- [x] 延迟实测：800×600 约 311 ms、640×480 约 276 ms；
- [x] 开关标签页 10 次稳定；
- [x] 1 小时推流：26856 帧、dropped 8、RSS/fd 不增长；

### v2.0 可写简历

- 分层重构；
- 单元测试；
- 设计文档；
- 多客户端 / 鉴权 / HTTPS；
- Buildroot 精简镜像；
- 开机自启；
- 看门狗；
- 多线程双端演示（backlog，见第 10 节）。

---

## 4. 对照 12 周计划

原计划文件：

```text
D:\Desktop\deepseek_harness_project\嵌入式Linux学习规划-相机主线.md
```

当前对照：

| 周 | 计划 | 当前状态 |
|---|---|---|
| W0 | CMake、log、ini、gdbserver、Git | ✅ 完成 |
| W1 | 一帧 YUYV → RGB565 → fb0 | ✅ 完成（800×600 1:1） |
| W2 | 连续采集、FPS | ✅ 完成（15fps，0 丢帧） |
| W3 | 查表、letterbox、多分辨率 | ✅ 完成（优化 + 居中 + 640×480@30） |
| W4 | 触摸/UI/OSD | ✅ 完成（触摸、OSD） |
| W5 | libjpeg、存卡 | ✅ 完成（MJPEG 直存，不需要 libjpeg） |
| W6 | HTTP MJPEG | ✅ 完成（`/snapshot.jpg` + `/stream` + 首页） |
| W7 | 重构/测试/文档 | 🟡 文档已补，测试未做 |
| W8–W9 | I2C/Input 驱动 | ✅ 字符设备/poll + platform/input + I2C 光感（IIO）+ 自动亮度 |
| W10 | V4L2 深入 | 🟡 枚举/格式有实践 |
| W11 | Buildroot/自启/看门狗 | ❌ 未开始 |
| W12 | 性能/README/演示 | 🟡 README 已更新，8h + 1h 推流数据已有 |

结论：

> **W0–W6 完成，v1.5 已打标签；W8–W9 的四块（字符设备、poll、platform/input、
> I2C 光感）都跑通了，剩下的是产品侧：设置菜单、相册、v2.0。**

---

## 5. v0.5 实施记录（已完成）

### 第 1 步：单缓冲连续循环

在 `test_camera_preview.c` 基础上：

```text
while (running) {
    poll(timeout);
    DQBUF;
    yuyv_to_rgb;
    QBUF;
    display;
    fps;
}
```

要求：

- `QBUF` 尽早；
- `poll` 超时；
- Ctrl-C 退出；
- 每秒打印 FPS/耗时；
- 先接受撕裂。

### 第 2 步：测性能

记录：

```text
fps
capture_ms
convert_ms
display_ms
```

如果 FPS 低：

- 把浮点 YUV 转整数/查表；
- 不再逐像素 `lcd_put_pixel()`；
- 直接写 RGB565，按行处理；
- 减少一次 memcpy。

### 第 3 步：双缓冲

- `yres_virtual = 2 * yres`；
- 两个显示 buffer；
- `FBIOPAN_DISPLAY`；
- 写后台 buffer，显示前台；
- 观察撕裂是否消失。

### 第 4 步：10 分钟稳定性

- 连续运行 10 分钟；
- 记录 FPS 波动；
- 检查 `/proc/<pid>/status` 的 VmRSS；
- Ctrl-C 清理。

### v0.5 验收（全部通过，2026-11-08）

- [x] 10 分钟不崩；
- [x] FPS 可打印（15.0）；
- [x] 无内存增长（6324 kB 恒定）；
- [x] Ctrl-C 干净退出；
- [x] 双缓冲可用（`FBIOPAN_DISPLAY` 全程无错误）。

### W3 性能与适配（已完成，2026-11-09）

目标不是提高 800×600 的 fps（摄像头锁死 15），而是省出 CPU 余量、让 640×480 能跑 30。

结果：

- 定点化 + 提色度项 + 内联 clamp：conv 35.1 → 14.0 ms；
- 按行写 framebuffer：draw 30.6 → 8.6 ms；
- CPU 忙占比 98% → 34%；
- 640×480@30 实测 30.0 fps，0 丢帧；letterbox 居中生效。

验收：

- [x] 800×600 仍 15fps，CPU 忙占比降到 34%；
- [x] 640×480 跑满 30fps；
- [x] 画面与优化前一致（色条 FULL/LIMIT 都正常）。

---

## 6. W8–W9 驱动（已完成）

四块都做完了：

- 字符设备：`hello_misc`，misc + 设备树节点，`open/read/write/ioctl`；
- poll / 等待队列：等待条件写成 `hello_len > *ppos`，用户态 `poll` 能等到数据；
- input 子系统：`button_plat`，两个 GPIO、两个键，`KEY_F1` 拍照、`KEY_F2` 切分辨率；
- I2C：`ap3216c`，先用户态直读确认寄存器和换算，再写 `i2c_driver` + IIO。

顺带把背光（`/sys/class/backlight`）和自动亮度策略（`autolight.c`）做完了。

下一步不是驱动了，是产品侧：

- 锁定触摸屏（防误触）；
- OSD 设置菜单（亮度、熄屏、governor）；
- 然后才是 v2.0 的重构。

多客户端、鉴权、本地预览+推流同时，仍然放在 v2.0。

---

## 7. 风险

| 风险 | 影响 | 对策 |
|---|---|---|
| 暗光下摄像头自动降帧 | 帧率从 30 掉到 25/20 | 正常现象；要看齐就手动设曝光 |
| 双缓冲 pan 不被驱动支持 | 退回单缓冲 | 已实现自动降级 |
| `bytesperline` 不匹配 | 画面斜 | 以后加 stride 参数 |
| 摄像头 800×600 只能 15fps | 帧率上不去 | 接受；要 30fps 走 640×480 |
| 触摸驱动范围报错 | 坐标偏 | 已用 `touch_set_range()` 覆盖 |
| 切到 800×600 固定 1.7 s | 操作有停顿 | 可接受；要快就只切小分辨率 |
| 网络第一次做 | 协议/并发容易乱 | 已用单客户端 + 300ms 空连接超时收住 |
| 屏幕背光抬高光感读数 | 越调越亮的正反馈 | 迟滞 + 连续 3 次去抖；真出现就把自动上限降到 90% |
| 推流时本地预览暂停 | 不能双端同时看 | 摄像头只有一种格式；要双端先测 libjpeg-turbo |
| iOS Safari 不支持 MJPEG 流 | 苹果手机看不了 | 用 `/snapshot.jpg` 或后续做 HLS/WebRTC |

---

## 8. 版本标签规则

- 只有达到版本验收，才打版本标签；
- 已打：`v0.1`、`v0.5`、`v1.0`、`v1.5`，另有里程碑标签
  `capture-milestone-1`、`lcd-yuyv-milestone`、`preview-30fps-letterbox-milestone`、
  `mjpeg-snapshot-milestone`、`resolution-switch-milestone`、`touch-input-milestone`；
- 每个标签附：
  - 能演示什么；
  - 不能做什么；
  - 已知问题；
  - 构建/运行命令。

---

## 9. 一句话

> **v1.5 已经能演示：LCD 预览、触摸/按键拍照、切分辨率、浏览器看 MJPEG 流（约 300ms）；
> 驱动四块（字符设备、poll、platform/input、I2C 光感）都跑通了，屏幕会跟着环境光调亮度。**

---

## 10. Backlog

### 多线程双端演示：LCD 预览 + 网页推流同时

想做的时候再翻这条，别临时起意。

**背景**：摄像头同一时刻只能输出一种格式，LCD 要 YUYV，浏览器要 MJPEG。
线程本身解决不了格式冲突，真正的成本是软件编解码。

**前置实验**：

1. 在板子上编 libjpeg-turbo；
2. 测 640×480 / 800×600 的 `YUYV → JPEG` encode 和 `JPEG → RGB` decode 耗时；
3. encode ≤ 30ms 才有戏；> 50ms 就直接放弃，保留“要么预览、要么推流”。

**如果要做**：

- 单线程版：采集 → LCD 转换 → JPEG 编码 → 非阻塞发送（慢客户端丢帧）；
- 真需要并发再上线程：采集线程 + 发送线程 + 环形缓冲，注意 `camera_t`/fd 的归属；
- 想让触摸控制反映到浏览器，还得加控制通道（HTTP 查询参数或 WebSocket）。

**时机**：v1.5 发布、W8–W9 驱动基本盘之后；不要插在驱动学习中间。

### 产品 M1 进度（2026-11-26）

M1 是“硬件控制闭环”，拆开看：

- [x] 板载按键：KEY1 拍照、KEY2 切分辨率（`button_plat` + input）；
- [x] 背光：`/sys/class/backlight/backlight/`，`max_brightness = 8`；
- [x] 上滑/下滑调亮度（`touch` 手势 + `backlight` 模块）；
- [x] 光感自动亮度：AP3216C 走 `i2c-0` 的 `0x1e`，驱动 + 两条后端；
- [x] 锁定触摸屏（防止误触拍照/切分辨率）；
- [ ] OSD 设置菜单：亮度、熄屏、governor；
- [ ] 相册/Web 管理：M3。

M1 还差“设置菜单”和一个小功能（锁定屏）。双端同时仍然排在 v2.0，
不要因为 M1 快收尾了就去动线程。
