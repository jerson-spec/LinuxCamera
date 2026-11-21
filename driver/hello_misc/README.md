# hello_misc

一个最小的 misc 字符设备，用来练三件事：

- `file_operations`：open/read/write/ioctl/poll；
- `copy_to_user` / `copy_from_user`；
- 等待队列：`wait_event_interruptible`、`poll_wait`、`wake_up_interruptible`。

## 接口

设备节点：`/dev/hello_misc`（`MISC_DYNAMIC_MINOR`）。

| 操作 | 行为 |
|---|---|
| `write` | 最多 255 字节，存到内核缓冲区，重置读位置，唤醒等待者 |
| `read` | 读回缓冲区；没数据时阻塞，`O_NONBLOCK` 返回 `-EAGAIN` |
| `ioctl(HELLO_IOC_RESET)` | 清空缓冲区 |
| `ioctl(HELLO_IOC_GETLEN)` | 取当前长度（`int`） |
| `poll` | 有数据时返回 `POLLIN`，否则等待 |

命令号：

```c
#define HELLO_IOC_MAGIC   'h'
#define HELLO_IOC_RESET   _IO(HELLO_IOC_MAGIC, 0)
#define HELLO_IOC_GETLEN  _IOR(HELLO_IOC_MAGIC, 1, int)
```

`'h'` 是随手选的魔数，正式驱动要去
`Documentation/ioctl/ioctl-number.txt` 里登记，避免和别人撞。

## 编译

```bash
make            # 需要 KDIR 指向已经编译过的内核树
make clean
```

`Makefile` 默认：

```make
KDIR ?= $(HOME)/100ask_imx6ull-sdk/Linux-4.9.88
CROSS_COMPILE ?= arm-buildroot-linux-gnueabihf-
```

换内核路径就 `make KDIR=...`；工具链不在 PATH 里就传绝对路径的
`CROSS_COMPILE=/.../bin/arm-buildroot-linux-gnueabihf-`。

`make` 会生成 `hello_misc.ko`，顺便用交叉编译器编出 `test_hello`。

## 上板

```bash
adb push hello_misc.ko test_hello /root/
adb shell
insmod /root/hello_misc.ko
chmod 666 /dev/hello_misc
dmesg | tail
```

`dmesg` 里应该看到：

```text
hello: /dev/hello_misc registered
```

### 测 poll

`test_hello` 现在只做 poll 测试：它先 `ioctl(RESET)` 清空，然后停在
`poll(fd, POLLIN, 5000)` 上。

终端 A：

```bash
/root/test_hello
# polling...
```

终端 B：

```bash
echo "hello poll" > /dev/hello_misc
```

终端 A 打印：

```text
got: hello poll
```

### 测 read / write / ioctl

当前 `test_hello.c` 没覆盖这三项，可以用 shell 粗测：

```bash
echo "abc" > /dev/hello_misc     # write
cat /dev/hello_misc              # read，会阻塞到下一次 write
```

ioctl 需要一个用户态小工具，可以后面补一个 `test_rw.c`。

### 卸载

```bash
rmmod hello_misc
```

## 已知的小问题

- `hello_read` 的等待条件只判断 `hello_len > 0`，数据读完之后再 `read`
  会返回 0，而不是继续等；更严谨的写法是判断 `hello_len > *ppos`，
  或者 open 时把读位置清零；
- `hello_poll` 同理，只要 `hello_len > 0` 就返回可读，读完之后还会报告
  `POLLIN`；
- 没有加锁，只适合单读者/单写者；
- 设备节点权限没在驱动里设，需要 `chmod 666`；
- `write` 超过 255 字节会截断，返回值是截断后的长度。

这些都不影响学习用，知道边界在哪就行。
