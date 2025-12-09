# 📘 Linux 平台总线 (Platform Bus) 驱动实战手册

**实验目标**：
不直接操作寄存器，而是通过**“设备树描述硬件 + Platform 驱动匹配”**的现代 Linux 驱动模式，实现一个虚拟 FIFO 设备的读写。

**前置条件**：

* 已完成 SD 卡制作（分区1为 FAT32，分区2为 EXT4）。
* 已编译好 Linux 5.15 内核。

---

## 🛠️ 第一阶段：修改设备树 (硬件描述)

**核心目的**：在不修改驱动代码的情况下，通过设备树告诉内核：“我有一个硬件，名字叫 `lemon,fifo-v1`，缓冲区大小是 256”。

### 1.1 找到正确的文件 (🔴 易错点)

* **错误操作**：修改了 `u-boot/arch/arm/dts/...`。
* **正确路径**：必须修改 **Linux 内核源码** 中的设备树。

```bash
cd ~/Embedded_study/linux-5.15
# 注意路径里有 boot
nano arch/arm/boot/dts/vexpress-v2p-ca9.dts
```

### 1.2 添加设备节点

在根节点 `/ { ... }` 的末尾（`};` 之前）添加：

```dts
    /* 自定义 FIFO 设备 */
    lemon_fifo_device {
        compatible = "lemon,fifo-v1"; /* 匹配暗号 */
        fifo-size = <256>;            /* 自定义属性 */
        status = "okay";
    };
```

### 1.3 编译设备树

```bash
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- dtbs
```

* **产物**：`arch/arm/boot/dts/vexpress-v2p-ca9.dtb` (记住这个新文件！)

---

## 💻 第二阶段：编写驱动与应用

### 2.1 建立目录

```bash
cd ~/Embedded_study
mkdir driver_platform_fifo
cd driver_platform_fifo
```

### 2.2 驱动代码 (`lemon_fifo.c`)

核心逻辑：注册 `platform_driver`，在 `probe` 中读取设备树属性，并注册 `miscdevice`。

```c
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>
#include <linux/of.h>

struct lemon_fifo_data {
    char *buffer;
    int size;
    int current_len;
};
static struct lemon_fifo_data *my_data;

/* --- 文件操作 --- */
static ssize_t fifo_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    int copy_len = (count > my_data->current_len) ? my_data->current_len : count;
    if (copy_len == 0) return 0;
    if (copy_to_user(buf, my_data->buffer, copy_len)) return -EFAULT;
    my_data->current_len = 0; // 读完清空
    return copy_len;
}

static ssize_t fifo_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
    int copy_len = (count > my_data->size) ? my_data->size : count;
    if (copy_from_user(my_data->buffer, buf, copy_len)) return -EFAULT;
    my_data->current_len = copy_len;
    return copy_len;
}

static struct file_operations fifo_fops = {
    .owner = THIS_MODULE,
    .read = fifo_read,
    .write = fifo_write,
};

static struct miscdevice fifo_misc = {
    .minor = MISC_DYNAMIC_MINOR, // 动态分配次设备号
    .name = "lemon_fifo",
    .fops = &fifo_fops,
};

/* --- Platform 驱动 --- */
static int lemon_fifo_probe(struct platform_device *pdev) {
    u32 dt_size = 64;
    of_property_read_u32(pdev->dev.of_node, "fifo-size", &dt_size); // 读设备树
  
    my_data = kzalloc(sizeof(struct lemon_fifo_data), GFP_KERNEL);
    my_data->size = dt_size;
    my_data->buffer = kzalloc(dt_size, GFP_KERNEL);
  
    misc_register(&fifo_misc); // 注册设备
    printk(KERN_INFO "[LemonFIFO] Probe called! Size: %d\n", dt_size);
    return 0;
}

static int lemon_fifo_remove(struct platform_device *pdev) {
    misc_deregister(&fifo_misc);
    kfree(my_data->buffer);
    kfree(my_data);
    return 0;
}

static const struct of_device_id lemon_dt_ids[] = {
    { .compatible = "lemon,fifo-v1" },
    { }
};
MODULE_DEVICE_TABLE(of, lemon_dt_ids);

static struct platform_driver lemon_fifo_driver = {
    .probe = lemon_fifo_probe,
    .remove = lemon_fifo_remove,
    .driver = { .name = "lemon_fifo_drv", .of_match_table = lemon_dt_ids },
};
module_platform_driver(lemon_fifo_driver);
MODULE_LICENSE("GPL");
```

### 2.3 应用代码 (`fifo_test.c`)

```c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

int main() {
    int fd = open("/dev/lemon_fifo", O_RDWR);
    if (fd < 0) { perror("Open failed"); return -1; }

    char msg[] = "Hello from User Space!";
    char buf[100] = {0};

    write(fd, msg, strlen(msg));
    printf("Wrote: %s\n", msg);

    read(fd, buf, sizeof(buf));
    printf("Read back: %s\n", buf);

    close(fd);
    return 0;
}
```

### 2.4 编译 (🔴 易错点)

* **Makefile 修正**：必须加 `KCFLAGS` 防止汇编报错，必须加 `-static` 防止应用运行报错。

```makefile
KDIR := /home/lemonsuqing/Embedded_study/linux-5.15
PWD := $(shell pwd)
CROSS := arm-linux-gnueabihf-
obj-m := lemon_fifo.o

all:
	# 编译驱动：指定架构防止 isb 指令报错
	make -C $(KDIR) M=$(PWD) modules ARCH=arm CROSS_COMPILE=$(CROSS) KCFLAGS="-march=armv7-a"
	# 编译应用：静态链接
	$(CROSS)gcc fifo_test.c -o fifo_test -static
clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -f fifo_test
```

执行 `make`，生成 `lemon_fifo.ko` 和 `fifo_test`。

---

## 💾 第三阶段：更新 SD 卡 (最关键的同步)

**此处是你之前最容易翻车的地方（只改了源码没更新到卡里）。**

```bash
cd ~/Embedded_study

# 1. 挂载 SD 卡
sudo losetup -P -f --show sd.img
# 假设是 /dev/loop0
sudo mount /dev/loop0p1 mount_boot
sudo mount /dev/loop0p2 mount_root

# 2. 更新 DTB (🔴 必须做！否则驱动无法匹配)
# 把新编译的设备树覆盖到 Boot 分区
sudo cp -v linux-5.15/arch/arm/boot/dts/vexpress-v2p-ca9.dtb mount_boot/

# 3. 放入驱动和应用
sudo mkdir -p mount_root/root/drivers
sudo cp driver_platform_fifo/lemon_fifo.ko mount_root/root/drivers/
sudo cp driver_platform_fifo/fifo_test     mount_root/root/drivers/

# 4. 卸载
sudo umount mount_boot mount_root
sudo losetup -d /dev/loop0
```

---

## 🚀 第四阶段：上机运行

### 4.1 启动 QEMU (🔴 易错点)

* **错误**：直接用 `-sd sd.img` 但当前目录下没有该文件。
* **正确**：使用绝对路径。

```bash
cd ~/Embedded_study/uboot_study/u-boot
qemu-system-arm -M vexpress-a9 -m 512M -nographic -kernel u-boot -sd ~/Embedded_study/sd.img
```

### 4.2 U-Boot 加载 (🔴 易错点)

* **错误**：忘记设置 `bootargs`，导致内核启动黑屏（无控制台）或 Panic（找不到根文件系统）。
* **正确**：必须指定 `root=/dev/mmcblk0p2`。

```bash
# 1. 从 SD 卡加载
load mmc 0:1 0x60100000 zImage
load mmc 0:1 0x62000000 vexpress-v2p-ca9.dtb

# 2. 设置参数 (SD卡启动专用)
setenv bootargs 'console=ttyAMA0,115200n8 debug earlyprintk root=/dev/mmcblk0p2 rootfstype=ext4 rw init=/init'

# 3. 启动
bootz 0x60100000 - 0x62000000
```

### 4.3 驱动测试 (🔴 易错点)

进入系统 Shell 后：

1. **加载驱动**

   ```bash
   cd /root/drivers
   insmod lemon_fifo.ko
   ```

   * **成功标志**：`[LemonFIFO] Probe called! Size: 256`。
   * *如果没有这句话，说明 DTB 没更新，回去重做第三阶段。*
2. **创建节点 (因为没有 mdev/udev)**

   ```bash
   # A. 查看次设备号
   cat /proc/misc
   # 假设看到:  56 lemon_fifo

   # B. 手动创建
   # 主设备号 10 是 misc 设备的固定值
   mknod /dev/lemon_fifo c 10 56
   ```
3. **运行应用**

   ```bash
   ./fifo_test
   ```

   * **成功标志**：
     * `Wrote: Hello...`
     * `Read back: Hello...`

---

### 🏆 总结：为什么这么做？

1. **改设备树**：为了模拟真实的硬件变更，验证驱动和硬件的解耦。
2. **Platform 驱动**：这是 Linux 驱动的主流框架，利用 `compatible` 字符串自动匹配，比写死的字符设备驱动更灵活。
3. **Misc 设备**：简化字符设备注册流程（自动申请主设备号），适合简单的驱动。
4. **手动 mknod**：因为我们的 RootFS 是极简的，没有热插拔守护进程，必须手动介入，这让你理解了设备文件的本质（主/次设备号的索引）。

这就是你刚才完成的全部工作！每一个报错都是对原理的一次加深理解。
