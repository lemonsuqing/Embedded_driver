git# 💡 LicheeRV Nano 学习笔记（二）：从命令到代码 —— C 语言自动化点灯

在第一章中，我们学会了通过 Shell 命令控制硬件。本章我们将通过 C 语言编写一个自动化程序，实现 LED 的定时闪烁。

## 一、 规范化的项目结构

在 Linux 开发中，良好的目录习惯能让开发事半功倍。

```bash
# 创建并进入专门的实验目录
mkdir -p ~/LicheeRV_Nano_Study/drivers/led_driver
cd ~/LicheeRV_Nano_Study/drivers/led_driver
```

---

## 二、 编写 C 语言程序

使用 `nano led_blink.c` 编写以下代码。这段代码的逻辑实际上是把我们第一章学到的 Shell 命令“程序化”。

### 核心逻辑解析：

1. **解除内核占用**：程序启动先向 `trigger` 文件写入 `none`。
2. **文件操作**：使用 `open()` 打开 `/sys/class/leds/led-user/brightness`。
3. **循环闪烁**：在 `while(1)` 死循环中，交替写入 `"1"` 和 `"0"`，并使用 `usleep()`（微秒级延迟）控制频率。

```c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main() {
    // 1. 夺回控制权：设置 trigger 为 none
    int fd_trig = open("/sys/class/leds/led-user/trigger", O_WRONLY);
    write(fd_trig, "none", 4);
    close(fd_trig);

    // 2. 准备控制亮度
    int fd_led = open("/sys/class/leds/led-user/brightness", O_WRONLY);

    printf("LicheeRV Nano LED 自动化程序已启动...\n");
    printf("控制权已夺回，正在以 0.5s 频率闪烁。按 Ctrl+C 退出。\n");

    while(1) {
        write(fd_led, "1", 1);  // 点亮
        usleep(500000);         // 延迟 0.5 秒
        write(fd_led, "0", 1);  // 熄灭
        usleep(500000);         // 延迟 0.5 秒
    }

    close(fd_led);
    return 0;
}
```

---

## 三、 关键：静态编译 (Static Compilation)

这是新手最容易遇到的“灵异事件”：**程序明明在，权限也给了，运行却报 `-sh: ./xxx: not found`。**

### 1. 为什么会这样？

* **动态编译（默认）**：程序运行像“点外卖”，运行时才去板子的系统里找 `glibc` 等库。如果板子系统里的库（如 Sipeed 用的 musl）和编译器提供的不一致，就会报错。
* **静态编译（`-static`）**：程序运行像“自带干粮”，把所有依赖的库全部打包进二进制文件。程序变大了，但**不挑环境，哪里都能跑**。

### 2. 编译命令

```bash
riscv64-linux-gnu-gcc -static led_blink.c -o led_blink
```

---

## 四、 部署：SD 卡传输全流程

由于虚拟机与 SD 卡的连接是动态的，请务必遵循“**先看、再挂、后拷、必卸**”的原则。

1. **先看 (确认盘符)**：执行 `lsblk` 确认 SD 卡 ROOTFS 分区（通常是第二个分区，如 `sdb2`）。
2. **再挂 (挂载)**：
   ```bash
   sudo mount /dev/sdb2 ~/temp_sd
   ```
3. **后拷 (拷贝)**：
   ```bash
   sudo cp led_blink ~/temp_sd/root/
   ```
4. **必卸 (同步与卸载)**：**这是保证文件不损坏的关键！**
   ```bash
   sync                # 强制将内存缓存写入 SD 卡
   sudo umount ~/temp_sd
   ```

---

## 五、 板端运行与观察

插回 SD 卡，上电启动 LicheeRV Nano。

```bash
cd /root
chmod +x led_blink
./led_blink
```

### 现象观察：

* **瞬间夺权**：运行程序的一瞬间，原本由系统 `activity` 触发器控制的“乱闪”会立刻停止，转为你设定的稳定闪烁。
* **阻塞运行**：因为是 `while(1)` 循环，终端会一直停在运行界面。
* **优雅退出**：按下 `Ctrl + C` 结束程序。此时 `trigger` 依然是 `none`，灯会保持退出前的最后一刻状态（常亮或常灭）。

---

## 六、 总结与对比


| 维度         | 第一章：Shell 命令               | 第二章：C 语言程序           |
| :----------- | :------------------------------- | :--------------------------- |
| **控制方式** | 人工输入，单次执行               | 自动化，循环执行             |
| **性能**     | 慢（每次执行 echo 都要开关文件） | 快（文件打开一次，持续写入） |
| **灵活性**   | 适合快速调试、验证硬件           | 适合复杂的逻辑开发、产品化   |
| **部署难度** | 无需部署，直接输入               | 需要交叉编译、SD 卡传输      |
