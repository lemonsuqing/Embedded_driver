# 第十四阶段：编写第一个内核驱动 (Hello World)

#### 1. 核心概念：什么是内核模块 (.ko)？

* **裸机开发**：你写好代码，编译成固件，烧进去。如果要修改一个 LED 的闪烁频率，你必须重新编译整个工程，重新烧录。
* **Linux 内核**：内核非常庞大。如果你想加一个新硬件驱动，难道要重新编译几百兆的内核吗？不需要！
* **内核模块 (Kernel Module)**：Linux 允许你把一部分代码编译成独立的 **`.ko` 文件**。
  * **动态加载 (`insmod`)**：系统运行时，把这个文件插进内核里，驱动立马生效。
  * **动态卸载 (`rmmod`)**：不用了就拔出来，释放内存。
  * 这就像给电脑插拔 USB 鼠标一样方便，不需要重启系统。

---

#### 2. 准备工作

我们要创建一个独立的目录来存放驱动代码。不要混在内核源码或 U-Boot 源码里。

在 Ubuntu 终端执行：

```bash
cd ~/Embedded_study
mkdir driver_hello
cd driver_hello
```

---

#### 3. 第一步：编写驱动源码 (`hello_drv.c`)

使用 `nano hello_drv.c` 创建文件，输入以下代码。
*(请仔细阅读注释，每一行都是新知识)*

```c
#include <linux/module.h>   // 所有模块都需要的头文件
#include <linux/init.h>     // 指定初始化和退出函数
#include <linux/kernel.h>   // 包含 printk 等内核常用函数

/* 
 * 1. 模块加载函数 (__init)
 * 当你执行 insmod 时，内核会调用这个函数。
 * 类似于 C 语言的 main()，但它是内核视角的入口。
 */
static int __init hello_init(void)
{
    // printk 是内核里的 printf。
    // KERN_INFO 是日志级别，表示这是一条普通信息。
    printk(KERN_INFO "[Driver] Hello World! I am entering Kernel Space!\n");
    return 0; // 返回 0 表示加载成功
}

/* 
 * 2. 模块卸载函数 (__exit)
 * 当你执行 rmmod 时，内核会调用这个函数。
 * 必须在这里把占用的资源（内存、中断、GPIO）释放掉，否则会导致内存泄漏。
 */
static void __exit hello_exit(void)
{
    printk(KERN_INFO "[Driver] Goodbye! I am leaving Kernel Space!\n");
}

/* 
 * 3. 注册宏
 * 告诉内核，上面哪两个函数是入口和出口。
 */
module_init(hello_init);
module_exit(hello_exit);

/* 
 * 4. 模块信息 (许可证)
 * 必须声明 GPL 协议，否则内核会报 "Tainted" (受污染) 警告，甚至拒绝加载。
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("LemonSuqing");
MODULE_DESCRIPTION("A simple Hello World Module");
```

---

#### 4. 第二步：编写 Makefile (构建规则)

内核模块的编译非常特殊，**它必须依赖 Linux 内核的源码树**。因为驱动里调用的 `printk` 等函数，定义都在内核源码里。

创建 `Makefile` 文件 (注意文件名 M 大写)：
`nano Makefile`

**填入以下内容 (注意 Tab 缩进)：**

```makefile
# 1. 指定内核源码路径
# 这里的路径必须是你之前编译过的那个 linux-5.15 的绝对路径！
KDIR := /home/lemonsuqing/Embedded_study/linux-5.15

# 2. 获取当前目录
PWD := $(shell pwd)

# 3. 核心变量：告诉内核构建系统，要把 hello_drv.c 编译成模块 (obj-m)
obj-m := hello_drv.o

# 4. 编译规则
# -C $(KDIR): 切换到内核源码目录，借用它的 Makefile
# M=$(PWD): 告诉内核，驱动源码在外面这个目录
# modules: 编译模块的目标
all:
	make -C $(KDIR) M=$(PWD) modules ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-

clean:
	make -C $(KDIR) M=$(PWD) clean
```

> **注意**：`make` 命令下面那两行（`make -C ...`），必须以 **Tab 键**开头，不能用空格！

---

#### 5. 第三步：编译驱动

在 `driver_hello` 目录下执行：

```bash
make
```

**预期结果**：
如果不报错，你会看到生成了一个 **`hello_drv.ko`** 文件。
这就是我们的“驱动插件”！

---

#### 6. 第四步：植入 SD 卡

我们要把这个 `.ko` 文件放到 SD 卡的 RootFS 分区里，这样启动后的 Linux 才能访问到它。

*(这是对之前 SD 卡挂载操作的复习)*

```bash
cd ~/Embedded_study

# 1. 挂载 SD 卡镜像 (会自动识别分区)
sudo losetup -P -f --show sd.img
# 假设是 /dev/loop0 (如果不是，请改号)

# 2. 挂载 RootFS 分区 (分区2)
sudo mount /dev/loop0p2 mount_root

# 3. 复制驱动文件
# 我们把它放到 /root 目录下
sudo cp driver_hello/hello_drv.ko mount_root/root/

# 4. 卸载
sudo umount mount_root
sudo losetup -d /dev/loop0
```

---

#### 7. 第五步：上机验证 (见证内核空间)

启动 QEMU：

```bash
cd ~/Embedded_study/uboot_study/u-boot

# 记得加 -sd 参数
qemu-system-arm \
    -M vexpress-a9 \
    -m 512M \
    -nographic \
    -kernel u-boot \
    -sd ~/Embedded_study/sd.img
```

**U-Boot 启动命令** (还是老样子，如果没改 bootcmd 就手动输)：

```bash
# 从 SD 卡加载
load mmc 0:1 0x60100000 zImage
load mmc 0:1 0x62000000 vexpress-v2p-ca9.dtb
# 启动参数 (SD卡启动)
setenv bootargs 'console=ttyAMA0,115200n8 debug earlyprintk root=/dev/mmcblk0p2 rootfstype=ext4 rw init=/init'
# 启动
bootz 0x60100000 - 0x62000000
```

**进入 Linux Shell 后：**

1. **检查驱动文件在不在**

   ```bash
   cd /root
   ls
   ```

   *应该能看到 `hello_drv.ko`*
2. **加载驱动 (insmod)**

   ```bash
   insmod hello_drv.ko
   ```

   * **现象**：你应该立刻看到屏幕打印出 `[Driver] Hello World! I am entering Kernel Space!`。
   * *如果没有直接打印，输入 `dmesg` 查看内核日志缓冲区。*
3. **查看已加载模块 (lsmod)**

   ```bash
   lsmod
   ```

   * **现象**：列表中应该有 `hello_drv`。
4. **卸载驱动 (rmmod)**

   ```bash
   rmmod hello_drv
   ```

   * **现象**：屏幕打印出 `[Driver] Goodbye! ...`。

---

### 📝 知识点总结 (为什么这么做？)

1. **User Space vs Kernel Space**:

   * 之前的 `lemon_app` (printf) 运行在**用户空间**。它崩溃了只影响它自己。
   * 现在的 `hello_drv` (printk) 运行在**内核空间**。它拥有最高权限。**如果在这个代码里写个死循环或者空指针访问，整个系统（包括所有 APP）都会立刻死机。**这就是为什么驱动开发难。
2. **Makefile 的魔法**:

   * 为什么编译驱动需要内核源码？因为 `.ko` 本质上是内核的一部分，它必须知道内核里的数据结构（如 `struct module`）长什么样。如果内核源码变了，驱动必须重编。
3. **printk**:

   * 为什么不用 printf？因为 printf 是 C 标准库 (`glibc/uclibc`) 提供的，内核里没有标准库！内核只能用自己实现的 `printk`。

---

### 🚀 你的作业

请完成上述步骤，并截图：

1. **编译成功**：在 Ubuntu 下 `ls -l hello_drv.ko` 的结果。
2. **运行成功**：在 QEMU 里执行 `insmod` 和 `rmmod` 时的打印信息。

这一步跑通，你就掌握了**“如何向运行中的 Linux 植入代码”**的核心技能。这通常是驱动开发工程师 80% 的日常工作流。
