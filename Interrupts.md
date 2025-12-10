# 📘 Linux 驱动进阶：中断处理实战 (终极版)

**实验目标**：
在 QEMU 中模拟一个连接到中断控制器的按键。编写驱动申请中断 (`request_irq`)，当按键触发时，内核自动调用中断处理函数打印 `BANG!`。
*(注：由于 QEMU 无物理按键，驱动中包含一个定时器来模拟硬件电平信号)*

**前置条件**：

* 工作目录：`~/Embedded_study`
* 内核源码：`linux-5.15`
* U-Boot：已配置为保存环境变量到 SD 卡 (FAT)。

---

## 🛠️ 第一阶段：修改设备树 (硬件描述)

**核心坑点修复**：Vexpress-A9 的中断控制器标签是 `&gic`，不是 `&gpio` 也不是 `&intc`。

### 1.1 编辑 DTS 文件

**路径**：`~/Embedded_study/linux-5.15/arch/arm/boot/dts/vexpress-v2p-ca9.dts`

```bash
cd ~/Embedded_study/linux-5.15
nano arch/arm/boot/dts/vexpress-v2p-ca9.dts
```

### 1.2 添加中断节点

请按 `PageDown` 翻到文件**最末尾**，在最后一个 `};` 之前，插入以下代码：

```dts
    /* 自定义中断按键 */
    lemon_key {
        compatible = "lemon,key-v1";
      
        /* 1. 指定中断父亲：通用中断控制器 (GIC) */
        interrupt-parent = <&gic>;
      
        /* 2. 中断参数：<共享类型SPI(0), 中断号(50), 触发方式上升沿(1)> */
        interrupts = <0 50 1>; 
      
        status = "okay";
    };
```

*(保存退出：Ctrl+O -> 回车 -> Ctrl+X)*

### 1.3 编译 DTB

```bash
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- dtbs
```

**检查点**：必须显示 `DTC ... vexpress-v2p-ca9.dtb` 且无 Error。

---

## 💻 第二阶段：编写中断驱动 (Software)

### 2.1 创建工程目录

```bash
cd ~/Embedded_study
mkdir -p driver_irq_key
cd driver_irq_key
```

### 2.2 驱动源码 (`key_drv.c`)

创建 `nano key_drv.c`，填入以下内容：

```c
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of.h>

/* 定义私有数据结构 */
struct key_dev {
    int irq;                 // 保存申请到的中断号
    struct timer_list timer; // 用于在QEMU里模拟硬件信号
};

/* 
 * --- 中断处理函数 (Top Half) ---
 * 当硬件产生中断时，CPU 暂停当前工作，立即跳到这里执行。
 * 要求：快进快出，严禁休眠！
 */
static irqreturn_t key_handler(int irq, void *dev_id)
{
    printk(KERN_INFO "[KeyDriver] 💥 BANG! Interrupt Triggered! (IRQ %d)\n", irq);
    return IRQ_HANDLED; // 告诉内核：我处理完了
}

/* 
 * --- 模拟硬件触发器 ---
 * 真实硬件不需要这个。因为 QEMU 没法按物理按键，
 * 我们用定时器每隔 3 秒手动触发一次中断处理函数，
 * 假装是硬件发出的信号。
 */
static void simulate_hardware_press(struct timer_list *t)
{
    struct key_dev *dev = from_timer(dev, t, timer);
  
    // 直接调用处理函数，模拟中断发生
    key_handler(dev->irq, dev);
  
    // 3秒后再触发一次
    mod_timer(&dev->timer, jiffies + msecs_to_jiffies(3000));
}

static int key_probe(struct platform_device *pdev)
{
    struct key_dev *dev;
    int ret;

    printk(KERN_INFO "[KeyDriver] Probe start...\n");

    dev = devm_kzalloc(&pdev->dev, sizeof(struct key_dev), GFP_KERNEL);
    if (!dev) return -ENOMEM;

    /* 1. 从设备树获取中断号 */
    /* 对应 DTS 里的 interrupts = <0 50 1> */
    dev->irq = platform_get_irq(pdev, 0);
    if (dev->irq < 0) {
        printk(KERN_ERR "[KeyDriver] Failed to get IRQ\n");
        return -EINVAL;
    }
    printk(KERN_INFO "[KeyDriver] Got IRQ number: %d\n", dev->irq);

    /* 2. 申请中断 (核心步骤) */
    /* 参数: 中断号, 处理函数, 触发标志, 名字, 私有数据 */
    ret = request_irq(dev->irq, key_handler, IRQF_TRIGGER_RISING, "lemon_key_irq", dev);
    if (ret) {
        printk(KERN_ERR "[KeyDriver] Failed to request IRQ %d\n", dev->irq);
        return ret;
    }

    /* 3. 初始化定时器 (仅用于模拟) */
    timer_setup(&dev->timer, simulate_hardware_press, 0);
    mod_timer(&dev->timer, jiffies + msecs_to_jiffies(3000));

    platform_set_drvdata(pdev, dev);
    return 0;
}

static int key_remove(struct platform_device *pdev)
{
    struct key_dev *dev = platform_get_drvdata(pdev);
  
    del_timer_sync(&dev->timer); // 关掉定时器
    free_irq(dev->irq, dev);     // 释放中断
  
    printk(KERN_INFO "[KeyDriver] Removed\n");
    return 0;
}

static const struct of_device_id key_dt_ids[] = {
    { .compatible = "lemon,key-v1" },
    { }
};
MODULE_DEVICE_TABLE(of, key_dt_ids);

static struct platform_driver key_driver = {
    .probe = key_probe,
    .remove = key_remove,
    .driver = {
        .name = "lemon_key_drv",
        .of_match_table = key_dt_ids,
    },
};

module_platform_driver(key_driver);
MODULE_LICENSE("GPL");
```

### 2.3 构建脚本 (`Makefile`)

```makefile
KDIR := /home/lemonsuqing/Embedded_study/linux-5.15
PWD := $(shell pwd)
CROSS := arm-linux-gnueabihf-
obj-m := key_drv.o

all:
	make -C $(KDIR) M=$(PWD) modules ARCH=arm CROSS_COMPILE=$(CROSS) KCFLAGS="-march=armv7-a"
clean:
	make -C $(KDIR) M=$(PWD) clean
```

### 2.4 编译

```bash
make
```

**检查点**：生成 `key_drv.ko`。

---

## 💾 第三阶段：部署到 SD 卡 (严谨步骤)

**坑点提醒**：必须同时更新 DTB 和 KO 文件。

```bash
cd ~/Embedded_study

# 1. 挂载 (自动寻找空闲 loop)
LOOP_DEV=$(sudo losetup -P -f --show sd.img)
# 挂载分区
sudo mount ${LOOP_DEV}p1 mount_boot
sudo mount ${LOOP_DEV}p2 mount_root

# 2. 更新 DTB (Boot分区)
sudo cp -v linux-5.15/arch/arm/boot/dts/vexpress-v2p-ca9.dtb mount_boot/

# 3. 拷贝驱动 (RootFS分区)
# 建议先清空旧的，防止混淆
sudo mkdir -p mount_root/root/drivers
sudo cp -v driver_irq_key/key_drv.ko mount_root/root/drivers/

# 4. 卸载
sudo umount mount_boot mount_root
sudo losetup -d $LOOP_DEV
```

---

## 🚀 第四阶段：上机验证 (见证奇迹)

**坑点修复**：使用 `-drive` 参数替代 `-sd`，避免 QEMU 报 raw 格式警告，确保 U-Boot 环境变量读写正常。

### 4.1 启动 QEMU (使用标准命令)

```bash
cd ~/Embedded_study/uboot_study/u-boot

qemu-system-arm \
    -M vexpress-a9 \
    -m 512M \
    -nographic \
    -kernel u-boot \
    -drive if=sd,format=raw,file=$HOME/Embedded_study/sd.img
```

### 4.2 观察启动

* 因为你之前已经修复了 U-Boot 环境变量，所以此时应该会**自动倒计时 -> 加载内核 -> 进入 Lemon-OS 系统**。
* 如果没有自动进入，请参考上一轮对话手动设置 `bootargs`。

### 4.3 加载驱动

进入 Linux Shell 后：

```bash
cd /root/drivers
insmod key_drv.ko
```

### 4.4 预期现象

1. **Probe 成功**：

   ```text
   [KeyDriver] Probe start...
   [KeyDriver] Got IRQ number: 50
   ```

   *(如果是 50 或 40+ 的数字，说明 DTB 解析成功)*
2. **中断触发 (每隔 3 秒)**：

   ```text
   [KeyDriver] 💥 BANG! Interrupt Triggered! (IRQ 50)
   [KeyDriver] 💥 BANG! Interrupt Triggered! (IRQ 50)
   ...
   ```
3. **验证中断系统**：
   在打印间隙输入：

   ```bash
   cat /proc/interrupts
   ```

   **检查**：找到 `lemon_key_irq` 这一行，看看右边的计数器是不是在不断增加。
4. **卸载测试**：

   ```bash
   rmmod key_drv
   ```

   打印应立即停止。

---

### 🎉 总结

按照这份手册，你成功实现了：

1. **DTS 描述中断**：使用 `&gic` 正确连接中断控制器。
2. **驱动申请中断**：使用 `request_irq` 注册服务。
3. **异步处理**：验证了 CPU 正常工作时，驱动可以随时打断并执行逻辑。

**这就是嵌入式 Linux 驱动开发中最核心的技能之一。** 恭喜通关！
