# 性能记录

> 当前阶段：v0.1 Camera Preview。
> 本文只记录**实测数据**和**下一阶段要测的指标**，不写空泛目标。

---

## 1. 当前状态

- 单帧预览：摄像头 800×600 YUYV → RGB888 → `lcd_put_pixel()` → LCD；
- 每帧约 480,000 个像素；
- 每个像素一次函数调用、一次 RGB565 打包、一次 `memcpy`；
- 当前只显示一帧，并保持 `lcd.ini` 的 `display_time` 秒；
- **还没有连续 FPS 数据**；
- 还没有测 `capture_ms` / `convert_ms` / `display_ms`。

---

## 2. 为什么现在性能不是重点

v0.1 的目标是“能显示”，不是“跑 30fps”。  
当前逐像素实现适合验证正确性，不适合连续预览。

---

## 3. v0.5 要采集的指标

每次运行记录：

| 指标 | 含义 | 工具 |
|---|---|---|
| `fps` | 每秒显示帧数 | 程序日志 |
| `capture_ms` | `poll + DQBUF` 耗时 | `clock_gettime` |
| `convert_ms` | YUYV → RGB 耗时 | `clock_gettime` |
| `display_ms` | 写 LCD 耗时 | `clock_gettime` |
| `dropped` | 丢帧数 | `buf.sequence` 差值 |
| `VmRSS` | 进程内存 | `/proc/<pid>/status` |

建议日志格式：

```text
fps=12.3 capture=2.1ms convert=18.5ms display=35.2ms dropped=0
```

---

## 4. 当前实现的主要开销

### 4.1 `lcd_put_pixel()` 逐像素

```c
for (i = 0; i < h; i++)
    for (j = 0; j < w; j++)
        lcd_put_pixel(j, i, color);
```

每像素：

- 函数调用；
- 边界判断；
- 计算 `pos`；
- RGB888 → RGB565；
- `memcpy` 2 字节。

800×600 一帧 48 万次，开销很大。

### 4.2 YUYV 转换使用浮点

`yuyv_to_rgb()` 每个像素做多次浮点乘加。  
Cortex-A7 有 FPU，但大量浮点仍然不便宜。

### 4.3 中间 RGB888 buffer

当前路径：

```text
YUYV buffer → RGB888 buffer → 逐像素 RGB565 → LCD
```

多了一次 RGB888 内存写和读。

---

## 5. 优化顺序

先测量，再优化；不要凭感觉。

### 第一优先级：直接写 RGB565

把：

```text
YUYV → RGB888 → lcd_put_pixel → RGB565
```

改成：

```text
YUYV → RGB565 → 按行写 framebuffer
```

减少函数调用和中间 buffer。

### 第二优先级：整数/查表转换

把浮点系数改成定点：

```text
r = (298*c + 409*e + 128) >> 8
g = (298*c - 100*d - 208*e + 128) >> 8
b = (298*c + 516*d + 128) >> 8
```

或者预先算表。

### 第三优先级：按行处理

利用：

- 源 `bytesperline`；
- 目标 `line_length`；
- 每行连续写，不要逐像素跨行。

### 第四优先级：双缓冲

- 避免撕裂；
- 允许一边转换一边显示；
- `FBIOPAN_DISPLAY` 切页。

---

## 6. 目标（暂定，需实测校准）

| 分辨率 | 目标 FPS | 说明 |
|---|---|---|
| 800×600 | ≥ 15–20 | 软件转换 + framebuffer |
| 640×480 | ≥ 20–25 | 更适合 6ULL |
| 1024×600 | 先不要求 | 摄像头不支持，且像素更多 |

注意：USB 带宽、LCD 总线、CPU 都会影响，最终以实测为准。

---

## 7. 内存检查

运行：

```bash
while true; do
    grep VmRSS /proc/$(pidof test_camera_preview)/status
    sleep 60
done
```

v0.5 要求：

- 连续 10 分钟 VmRSS 基本稳定；
- 不因每帧 malloc/free 产生碎片；
- 采集 buffer 不泄漏；
- 显示 buffer 不重复 mmap。

---

## 8. 待填数据

> 完成 v0.5 连续预览后，把实测数据填在这里。

### 测试环境

- 板子：
- 摄像头：
- 分辨率：
- 格式：
- LCD：
- 编译类型：

### 结果

| 指标 | 数值 |
|---|---|
| FPS | 待测 |
| capture_ms | 待测 |
| convert_ms | 待测 |
| display_ms | 待测 |
| VmRSS 10min | 待测 |
| dropped | 待测 |

---

## 9. 一句话

> **v0.1 不追性能；v0.5 先测量，再优化。**
