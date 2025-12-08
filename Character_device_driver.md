# 📘 Linux 字符设备驱动开发实战

**目标**：
编写一个内核驱动，模拟一个 LED 灯。

1. **应用层**：程序发送 1，灯亮；发送 0，灯灭。
2. **内核层**：驱动接收数据，打印日志模拟控制硬件。

---

## 🛠️ 阶段 0：预备工作 (修复内核依赖)

在编译驱动之前，必须确保内核源码树已经准备好了“模块编译环境”（生成符号表）。你之前报错 `Module.symvers is missing` 就是因为缺这一步。

**在 Ubuntu 终端执行：**

```bash
cd ~/Embedded_study/linux-5.15

# 1. 清理 (防止残留配置干扰)
make distclean

# 2. 配置 Vexpress
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- vexpress_defconfig

# 3. 编译内核模块基础设施 (关键！这一步生成 Module.symvers)
# -march=armv7-a 防止指令集报错
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules_prepare -j8 KCFLAGS="-march=armv7-a"
```

*(注：`modules_prepare` 比 `modules` 快，专门用于准备外部驱动编译环境)*

---

## 📝 阶段 1：编写代码

创建独立目录：

```bash
cd ~/Embedded_study
mkdir driver_char_led
cd driver_char_led
```

### 1.1 驱动代码 (`led_drv.c`)

创建 `nano led_drv.c`。
**原理**：实现 `file_operations` 结构体，把 `open/write` 系统调用映射到具体的驱动函数。

```c
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h> // copy_from_user
#include <linux/init.h>
#include <linux/cdev.h>

#define DEVICE_NAME "my_led"

static int major_num;

// 对应应用层的 open()
static int led_open(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "[Driver] led_open: Device opened\n");
    return 0;
}

// 对应应用层的 write()
static ssize_t led_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    char val;
    int ret;

    // 核心原理：用户空间内存不能直接读，必须通过专用函数拷贝到内核空间
    ret = copy_from_user(&val, buf, 1);
    if (ret != 0) return -EFAULT;

    if (val == 1)
        printk(KERN_INFO "[Driver] ===> LED ON! (Value: 1)\n");
    else
        printk(KERN_INFO "[Driver] ===> LED OFF... (Value: 0)\n");

    return count;
}

// 对应 close()
static int led_close(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "[Driver] led_close: Device closed\n");
    return 0;
}

// 核心结构体：连接 上层系统调用 和 底层驱动函数
static struct file_operations led_fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .write = led_write,
    .release = led_close,
};

static int __init led_init(void)
{
    // 注册字符设备，0 表示让系统自动分配一个主设备号
    major_num = register_chrdev(0, DEVICE_NAME, &led_fops);
    if (major_num < 0) return major_num;
  
    printk(KERN_INFO "[Driver] Registered with major number %d\n", major_num);
    return 0;
}

static void __exit led_exit(void)
{
    unregister_chrdev(major_num, DEVICE_NAME);
    printk(KERN_INFO "[Driver] Goodbye!\n");
}

module_init(led_init);
module_exit(led_exit);
MODULE_LICENSE("GPL");
```

### 1.2 应用代码 (`led_app.c`)

创建 `nano led_app.c`。
**原理**：在用户空间通过标准文件 I/O 操作设备。

```c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char **argv)
{
    int fd;
    char val;

    if (argc != 2) {
        printf("Usage: %s <on|off>\n", argv[0]);
        return -1;
    }

    // 打开设备文件
    fd = open("/dev/my_led", O_RDWR);
    if (fd < 0) {
        printf("Can't open /dev/my_led\n");
        return -1;
    }

    // 准备数据
    if (strcmp(argv[1], "on") == 0) val = 1;
    else val = 0;

    // 写入数据 -> 触发驱动 led_write
    write(fd, &val, 1);
    close(fd);
    return 0;
}
```

---

## ⚙️ 阶段 2：编译构建 (修复版)

创建 `nano Makefile`。
**核心修正**：加入了 `KCFLAGS="-march=armv7-a"` 解决汇编报错，加入了 `-static` 解决应用运行报错。

```makefile
# 路径修改为你自己的实际路径
KDIR := /home/lemonsuqing/Embedded_study/linux-5.15
PWD := $(shell pwd)
CROSS := arm-linux-gnueabihf-

obj-m := led_drv.o

all:
	# 1. 编译驱动 (内核态)
	# KCFLAGS Fix: 强制指定架构，防止出现 isb/dsb 指令不支持的错误
	make -C $(KDIR) M=$(PWD) modules ARCH=arm CROSS_COMPILE=$(CROSS) KCFLAGS="-march=armv7-a"

	# 2. 编译应用 (用户态)
	# -static Fix: 静态链接，防止在板子上找不到动态库
	$(CROSS)gcc led_app.c -o led_app -static

clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -f led_app
```

**执行编译**：

```bash
make
```

*产物：`led_drv.ko` 和 `led_app`。*

---

## 💾 阶段 3：部署到 SD 卡

```bash
cd ~/Embedded_study

# 1. 挂载 SD 卡 (假设 loop0)
sudo losetup -P -f --show sd.img
sudo mount /dev/loop0p2 mount_root

# 2. 创建存放目录并拷贝
sudo mkdir -p mount_root/root/drivers
sudo cp driver_char_led/led_drv.ko mount_root/root/drivers/
sudo cp driver_char_led/led_app    mount_root/root/drivers/

# 3. 卸载
sudo umount mount_root
sudo losetup -d /dev/loop0
```

---

## 💻 阶段 4：上机运行 (完整流程)

### 4.1 启动 QEMU

```bash
cd ~/Embedded_study/uboot_study/u-boot
# 必须带 -sd 参数
qemu-system-arm \
    -M vexpress-a9 \
    -m 512M \
    -nographic \
    -kernel u-boot \
    -sd ~/Embedded_study/sd.img
```

### 4.2 U-Boot 加载 (如果没设自动启动)

```bash
load mmc 0:1 0x60100000 zImage
load mmc 0:1 0x62000000 vexpress-v2p-ca9.dtb
setenv bootargs 'console=ttyAMA0,115200n8 debug earlyprintk root=/dev/mmcblk0p2 rootfstype=ext4 rw init=/init'
bootz 0x60100000 - 0x62000000
```

*(如果之前设置过 `bootcmd`，直接 `run bootcmd` 即可)*

### 4.3 在 Linux Shell 中测试

**1. 进入目录**

```bash
cd /root/drivers
```

**2. 加载模块**

```bash
insmod led_drv.ko
```

* **现象**：打印 `[Driver] Registered with major number 248`。
* **注意**：记下这个数字 **248** (你的可能会变)。

**3. 手动创建设备节点 (Fix: 解决 Can't open)**
因为我们的最小系统没有运行 `udev/mdev` 守护进程，内核虽然注册了设备，但没有自动生成文件。我们需要手工捏一个。

```bash
# mknod <路径> <类型:c> <主设备号> <次设备号>
mknod /dev/my_led c 248 0
```

**4. 运行应用测试 (Fix: 解决 path not found)**
使用绝对路径或 `./` 运行。

* **开灯**：

  ```bash
  ./led_app on
  ```

  * **结果**：`[Driver] ===> LED ON! (Value: 1)`
* **关灯**：

  ```bash
  ./led_app off
  ```

  * **结果**：`[Driver] ===> LED OFF... (Value: 0)`

---

## 🧐 原理总结 (Why?)

1. **内核态与用户态隔离**：
   `led_app` 是不能直接控制硬件的。它只能通过 `open("/dev/my_led")` 发送系统调用。CPU 切换到内核态，执行 `led_open`。
2. **数据传输 (`copy_from_user`)**：
   应用层的 `val` 变量在用户内存，驱动不能直接指针访问。必须把数据“搬”过来。
3. **设备文件 (`/dev/my_led`)**：
   这只是一个入口（索引）。它的核心是 **主设备号 (Major Number)**。
   * 应用层操作文件 -> VFS 看到设备号 248 -> 找到注册号为 248 的驱动 (`led_drv`) -> 调用驱动函数。

这就是一个完整的字符设备驱动生命周期！保存好这份手册，这是你未来开发更复杂驱动的基础模板。
