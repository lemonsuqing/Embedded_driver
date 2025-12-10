# 📘 第六阶段：并发与竞态实战 (Mutex 互斥锁)

**银行账户模拟器**：

1. **场景**：驱动里有一个全局变量 `balance` (余额)，初始为 0。
2. **操作**：两个 APP 同时去“存款”。
   * APP A 存 50 次，每次存 1 元。
   * APP B 存 50 次，每次存 1 元。
3. **预期**：最后余额应该是 **100 元**。
4. **竞态**：如果不加锁，两个 APP 会同时读取旧余额，导致存款丢失，最后余额**小于 100 元**。

**实验目标**：

1. 编写一个有“竞态漏洞”的驱动，亲眼看到数据出错。
2. 加入 `struct mutex` (互斥体)，修复漏洞，保证数据 100% 正确。

**前置条件**：

* 工作目录：`~/Embedded_study`
* 已完成之前的环境搭建。

---

## 🛠️ 第一阶段：编写“有漏洞”的驱动

我们先写一个**没有锁**的驱动，还要故意在读写之间加延迟，把 bug 放大。

### 1.1 创建目录

```bash
cd ~/Embedded_study
mkdir -p driver_concurrency
cd driver_concurrency
```

### 1.2 编写驱动 (`race_drv.c`)

创建一个带有**开关**的驱动。我们将通过修改宏定义来演示“有锁”和“无锁”的区别。

```c
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/delay.h>  // 为了使用 ssleep
#include <linux/mutex.h>  // 互斥锁头文件

#define DRIVER_NAME "lemon_race"

/* 
 * 🔴 实验开关：
 * 0 = 不加锁 (演示数据出错)
 * 1 = 加互斥锁 (演示数据安全)
 */
#define USE_MUTEX_LOCK  0  

/* 全局共享资源 (相当于银行账户余额) */
static int shared_balance = 0;

/* 定义互斥锁 */
static struct mutex my_mutex;

struct race_dev {
    dev_t dev_num;
    struct cdev my_cdev;
    struct class *my_class;
    struct device *device;
};
static struct race_dev my_dev;

/* 打开文件 */
static int race_open(struct inode *inode, struct file *file)
{
    return 0;
}

/* 
 * 读函数：返回当前余额 
 */
static ssize_t race_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    int val = shared_balance;
    if (copy_to_user(buf, &val, sizeof(int)))
        return -EFAULT;
    return sizeof(int);
}

/* 
 * 写函数：模拟“取钱 -> 思考 -> 存钱”的过程
 * 这里故意制造了“时间缝隙”，让另一个 APP 有机会插进来捣乱
 */
static ssize_t race_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    int temp_val;
  
    /* === 临界区开始 (Danger Zone) === */
#if USE_MUTEX_LOCK
    // 申请锁：如果被别人锁了，我就在这里睡觉等待，直到别人解锁
    mutex_lock(&my_mutex); 
#endif

    /* 1. 读取当前余额 (从全局变量读到局部变量) */
    temp_val = shared_balance;

    /* 2. 模拟复杂的计算过程 (故意睡 20ms，让出 CPU 给别的进程) */
    /* 这就是竞态发生的根本原因：读和写之间被打断了！ */
    msleep(20); 

    /* 3. 余额 +1 */
    temp_val++;

    /* 4. 写回全局变量 */
    shared_balance = temp_val;

#if USE_MUTEX_LOCK
    // 释放锁：唤醒后面排队的人
    mutex_unlock(&my_mutex);
#endif
    /* === 临界区结束 === */

    return count;
}

static struct file_operations race_fops = {
    .owner = THIS_MODULE,
    .open = race_open,
    .read = race_read,
    .write = race_write,
};

static int __init race_init(void)
{
    /* 初始化互斥锁 */
    mutex_init(&my_mutex);
  
    /* 注册字符设备 (标准流程) */
    alloc_chrdev_region(&my_dev.dev_num, 0, 1, DRIVER_NAME);
    cdev_init(&my_dev.my_cdev, &race_fops);
    cdev_add(&my_dev.my_cdev, my_dev.dev_num, 1);
  
    my_dev.my_class = class_create(THIS_MODULE, DRIVER_NAME);
    my_dev.device = device_create(my_dev.my_class, NULL, my_dev.dev_num, NULL, DRIVER_NAME);

    printk(KERN_INFO "[RaceDrv] Initialized. Lock is %s\n", 
           USE_MUTEX_LOCK ? "ON (Safe)" : "OFF (Dangerous)");
    return 0;
}

static void __exit race_exit(void)
{
    device_destroy(my_dev.my_class, my_dev.dev_num);
    class_destroy(my_dev.my_class);
    cdev_del(&my_dev.my_cdev);
    unregister_chrdev_region(my_dev.dev_num, 1);
    printk(KERN_INFO "[RaceDrv] Exit\n");
}

module_init(race_init);
module_exit(race_exit);
MODULE_LICENSE("GPL");
```

### 1.3 编写测试应用 (`race_test.c`)

这个程序会疯狂地去存钱。

```c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

/* 
 * 每个进程存 50 次，每次 +1
 * 理论上如果跑 2 个进程，最后应该是 100
 */
#define LOOPS 50 

int main(int argc, char **argv)
{
    int fd;
    int val = 0;
    int i;

    fd = open("/dev/lemon_race", O_RDWR);
    if (fd < 0) {
        perror("Open failed");
        return -1;
    }

    printf("PID %d: Starting to deposit...\n", getpid());

    for (i = 0; i < LOOPS; i++) {
        // 写任何数据进去，驱动都会执行 +1 操作
        write(fd, &val, sizeof(int));
      
        // 打印进度 (可选)
        if (i % 10 == 0) printf("PID %d: %d/%d\n", getpid(), i, LOOPS);
    }

    printf("PID %d: Done!\n", getpid());
    close(fd);
    return 0;
}
```

### 1.4 构建脚本 (Makefile)

```makefile
KDIR := /home/lemonsuqing/Embedded_study/linux-5.15
PWD := $(shell pwd)
CROSS := arm-linux-gnueabihf-
obj-m := race_drv.o

all:
	make -C $(KDIR) M=$(PWD) modules ARCH=arm CROSS_COMPILE=$(CROSS) KCFLAGS="-march=armv7-a"
	$(CROSS)gcc race_test.c -o race_test -static

clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -f race_test
```

### 1.5 编译

```bash
make
```

*检查：生成 `race_drv.ko` 和 `race_test`。*

---

## 💾 第二阶段：部署与“灾难现场”演示

我们先跑**不加锁**的版本，看看世界是怎么毁灭的。

**1. 部署到 SD 卡**

```bash
cd ~/Embedded_study
sudo losetup -P -f --show sd.img
# 假设 loop0
sudo mount /dev/loop0p2 mount_root

# 拷贝
sudo mkdir -p mount_root/root/drivers
# 为了防止旧文件干扰，建议先删
sudo rm -f mount_root/root/drivers/race_*
sudo cp -v driver_concurrency/race_drv.ko mount_root/root/drivers/
sudo cp -v driver_concurrency/race_test    mount_root/root/drivers/

sudo umount mount_root
sudo losetup -d /dev/loop0
```

**2. 启动 QEMU**

```bash
cd ~/Embedded_study/uboot_study/u-boot
qemu-system-arm \
    -M vexpress-a9 \
    -m 512M \
    -nographic \
    -kernel u-boot \
    -drive if=sd,format=raw,file=$HOME/Embedded_study/sd.img
```

**3. 上机运行 (见证 BUG)**
进入 Linux Shell 后：

```bash
cd /root/drivers
insmod race_drv.ko
# 此时应该打印: [RaceDrv] Initialized. Lock is OFF (Dangerous)
```

**高潮来了：** 我们在后台同时运行两个存钱程序。

```bash
# 强制刷新设备树（若下列指令执行不了时输入）
mdev -s

./race_test & ./race_test &
```

*(注意：命令后面的 `&` 表示在后台运行，两个 `&` 表示两个程序同时跑)*

等待几秒钟，等它们都打印 `Done!` 后，我们查看最终余额：

```bash
# 读一下当前的数值 (cat 会把二进制转成乱码，我们用 dd 或者直接看驱动逻辑)
# 其实我们的 read 函数返回的是二进制 int。
# 为了方便看，我们写个简单命令读取，或者直接重新 insmod 既然没有 read 工具。
# 偷懒办法：修改 test 代码读出来，或者...
# 我们直接写个查看命令吧：
```

哎呀，刚才的 `race_test` 忘了写读取打印功能。不着急，我们在 Shell 里用 `hexdump` 看，或者重新写个小工具。

**临时补救：用 hexdump 查看**

```bash
hexdump -d /dev/lemon_race -n 4
```

* **理论值**：应该显示 `100`。
* **实际值**：你会发现它**远远小于 100** (比如 52, 60)。
* **原因**：两个进程同时读到 10，同时加 1，同时写回 11。明明跑了两次，结果只加了 1。**这就是数据丢失！**

---

## 🛠️ 第三阶段：开启互斥锁 (修复 BUG)

现在我们来修复它。

**1. 修改驱动源码**
回到 Ubuntu 终端，编辑 `driver_concurrency/race_drv.c`。
将第 17 行修改为：

```c
#define USE_MUTEX_LOCK  1  // <--- 开启锁保护
```

**2. 重新编译**

```bash
make
```

**3. 重新部署**
*(重复之前的 cp 操作)*

```bash
cd ~/Embedded_study
sudo losetup -P -f --show sd.img
sudo mount /dev/loop0p2 mount_root
sudo cp -v driver_concurrency/race_drv.ko mount_root/root/drivers/
sudo umount mount_root
sudo losetup -d /dev/loop0
```

---

## 🚀 第四阶段：验证修复结果

**1. 重启 QEMU**
**2. 加载新驱动**

```bash
cd /root/drivers
rmmod race_drv  # 如果之前没卸载
insmod race_drv.ko
# 此时必须打印: [RaceDrv] Initialized. Lock is ON (Safe)
```

**3. 再次并发运行**

```bash
./race_test & ./race_test &
```

你会发现这次运行速度变慢了。

* **原因**：因为加了锁，进程 A 在操作时，进程 B 被 `mutex_lock` 挡在外面睡觉。它们被迫从“并行”变成了“串行”。虽然慢了，但是**安全**。

**4. 查看结果**

```bash
hexdump -d /dev/lemon_race -n 4
```

* **结果**：这次屏幕上显示的数字一定是 **100** (或者 00100 之类的格式)。
* **结论**：数据严丝合缝，没有任何丢失。

---

### 🧐 核心原理总结

1. **临界区 (Critical Section)**：
   代码中访问共享资源（`shared_balance`）的那几行代码。
2. **互斥锁 (Mutex)**：
   * **原理**：就像洗手间的门锁。进去的人把门锁上，后面来的人只能在门口等（Sleep）。里面的人出来解锁了，后面的人才能进去。
   * **副作用**：会降低并发性能（排队需要时间），但在数据安全面前，这是必须的代价。
3. **自旋锁 (Spinlock) vs 互斥锁 (Mutex)**：
   * 如果我们在驱动里用了 `msleep` (模拟耗时操作)，**绝对不能用自旋锁**。因为自旋锁在等待时 CPU 是空转的（死等），不允许休眠。
   * 如果是简单的变量 `a++`，没有休眠，用自旋锁效率更高。

恭喜！你现在已经掌握了**“如何保护你的数据”**。这在多核 CPU 和多任务系统中是保命的技能。
