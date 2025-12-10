# 驱动工程模板

这是进阶路上的好问题。**工程化思维**就是：不要每次都从零开始写，而是建立一套**标准模板 (Boilerplate)**。

以后你要开发任何新驱动（比如温湿度传感器、继电器、蜂鸣器），只需要**复制这套文件夹，修改名字和逻辑**即可。

---

### 📂 1. 标准目录结构

建议你以后每个驱动都建立一个独立的文件夹，结构如下：

```text
my_new_driver/         <-- 项目根目录
├── Makefile           <-- 统一构建脚本
├── xxx_drv.c          <-- 驱动源码 (内核态)
├── xxx_test.c         <-- 测试应用 (用户态)
├── xxx_dts_snippet.txt <-- 设备树片段备份 (方便复制到内核DTS中)
└── deploy.sh          <-- 一键部署脚本 (防呆)
```

---

### 📝 2. 驱动通用模板 (`xxx_drv.c`)

这是基于 **Platform 总线** + **自动创建节点** 的现代驱动模板。
*你只需要修改中文注释标注的地方。*

```c
/* 
 * 通用 Platform 驱动模板 
 * 作者: LemonSuqing
 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/device.h>

/* --- 1. 定义配置宏 --- */
#define DRIVER_NAME "my_device"   // /dev/ 下的文件名
#define COMPATIBLE  "my,device-v1" // 必须和设备树完全一致！

/* --- 2. 私有数据结构 --- */
struct my_dev {
    dev_t dev_num;
    struct cdev my_cdev;
    struct class *my_class;
    struct device *device;
    // TODO: 在这里添加你的私有数据 (如 GPIO号, 寄存器基地址, 缓冲区等)
    int my_value; 
};

/* --- 3. 文件操作方法 --- */
static int my_open(struct inode *inode, struct file *file)
{
    struct my_dev *dev = container_of(inode->i_cdev, struct my_dev, my_cdev);
    file->private_data = dev;
    return 0;
}

static int my_release(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t my_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    struct my_dev *dev = file->private_data;
    // TODO: 实现读逻辑 (读取硬件数据 -> copy_to_user)
    return 0;
}

static ssize_t my_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    struct my_dev *dev = file->private_data;
    // TODO: 实现写逻辑 (copy_from_user -> 控制硬件)
    return count;
}

/* 接口定义 */
static struct file_operations my_fops = {
    .owner = THIS_MODULE,
    .open = my_open,
    .release = my_release,
    .read = my_read,
    .write = my_write,
};

/* --- 4. Platform 驱动逻辑 --- */
static int my_probe(struct platform_device *pdev)
{
    struct my_dev *dev;
    int ret;

    printk(KERN_INFO "[%s] Probe start...\n", DRIVER_NAME);

    /* A. 分配内存 */
    dev = devm_kzalloc(&pdev->dev, sizeof(struct my_dev), GFP_KERNEL);
    if (!dev) return -ENOMEM;

    /* B. 获取设备树信息 (TODO: 根据需要读取自定义属性) */
    // of_property_read_u32(pdev->dev.of_node, "my-prop", &val);

    /* C. 注册字符设备 */
    ret = alloc_chrdev_region(&dev->dev_num, 0, 1, DRIVER_NAME);
    if (ret < 0) return ret;

    cdev_init(&dev->my_cdev, &my_fops);
    ret = cdev_add(&dev->my_cdev, dev->dev_num, 1);
    if (ret < 0) goto err_cdev;

    /* D. 自动创建节点 /dev/xxx */
    dev->my_class = class_create(THIS_MODULE, DRIVER_NAME);
    if (IS_ERR(dev->my_class)) { ret = PTR_ERR(dev->my_class); goto err_class; }

    dev->device = device_create(dev->my_class, NULL, dev->dev_num, NULL, DRIVER_NAME);
    if (IS_ERR(dev->device)) { ret = PTR_ERR(dev->device); goto err_device; }

    /* E. 保存指针 */
    platform_set_drvdata(pdev, dev);
  
    printk(KERN_INFO "[%s] Probe success!\n", DRIVER_NAME);
    return 0;

err_device:
    class_destroy(dev->my_class);
err_class:
    cdev_del(&dev->my_cdev);
err_cdev:
    unregister_chrdev_region(dev->dev_num, 1);
    return ret;
}

static int my_remove(struct platform_device *pdev)
{
    struct my_dev *dev = platform_get_drvdata(pdev);
  
    // 销毁顺序与 Probe 相反
    device_destroy(dev->my_class, dev->dev_num);
    class_destroy(dev->my_class);
    cdev_del(&dev->my_cdev);
    unregister_chrdev_region(dev->dev_num, 1);
  
    printk(KERN_INFO "[%s] Removed\n", DRIVER_NAME);
    return 0;
}

/* 匹配表 */
static const struct of_device_id my_dt_ids[] = {
    { .compatible = COMPATIBLE },
    { }
};
MODULE_DEVICE_TABLE(of, my_dt_ids);

static struct platform_driver my_driver = {
    .probe = my_probe,
    .remove = my_remove,
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = my_dt_ids,
    },
};

module_platform_driver(my_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("LemonSuqing");
```

---

### 📱 3. 应用通用模板 (`xxx_test.c`)

```c
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>

/* 
 * 用法: ./app <cmd> [data]
 * 例如: ./app read
 *       ./app write hello
 */
int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: %s <read|write> [data]\n", argv[0]);
        return -1;
    }

    // 1. 打开设备 (修改设备名)
    int fd = open("/dev/my_device", O_RDWR);
    if (fd < 0) {
        perror("Open device failed");
        return -1;
    }

    // 2. 读操作
    if (strcmp(argv[1], "read") == 0) {
        char buf[1024] = {0};
        int len = read(fd, buf, sizeof(buf));
        if (len < 0) {
            perror("Read failed");
        } else {
            printf("Read: %s\n", buf);
        }
    }
    // 3. 写操作
    else if (strcmp(argv[1], "write") == 0) {
        if (argc < 3) {
            printf("Usage: %s write <string>\n", argv[0]);
        } else {
            int len = write(fd, argv[2], strlen(argv[2]));
            printf("Write %d bytes\n", len);
        }
    }

    close(fd);
    return 0;
}
```

---

### ⚙️ 4. 统一 Makefile

这一个 Makefile 可以同时编译驱动和应用。

```makefile
# --- 配置区域 ---
# 驱动文件名 (不带.c)
DRV_NAME := xxx_drv
# 应用文件名 (不带.c)
APP_NAME := xxx_test
# 内核路径 (根据你的实际情况修改)
KDIR := /home/lemonsuqing/Embedded_study/linux-5.15
# 交叉编译器
CROSS := arm-linux-gnueabihf-
# ----------------

PWD := $(shell pwd)
obj-m := $(DRV_NAME).o

all:
	@echo "--- Building Driver ---"
	make -C $(KDIR) M=$(PWD) modules ARCH=arm CROSS_COMPILE=$(CROSS) KCFLAGS="-march=armv7-a"
	@echo "--- Building App ---"
	$(CROSS)gcc $(APP_NAME).c -o $(APP_NAME) -static

clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -f $(APP_NAME)
```

---

### 📄 5. 设备树片段 (`xxx_dts.txt`)

这不是代码，是给你自己看的备忘录。告诉你需要在内核 `dts` 文件里加什么。

```dts
/* 将以下内容复制到 vexpress-v2p-ca9.dts 的根节点 / { ... } 最后 */

my_device_node {
    compatible = "my,device-v1"; /* 必须和驱动里的 COMPATIBLE 宏一致 */
    status = "okay";
  
    /* 自定义属性示例 */
    my-prop = <100>;
};
```

---

### 🚀 6. 一键部署脚本 (`deploy.sh`)

**神器**。有了它，你改完代码只需要执行 `./deploy.sh`，然后去 QEMU 重启即可。
*(需先 `chmod +x deploy.sh`)*

```bash
#!/bin/bash

# 配置路径
SD_IMG=~/Embedded_study/sd.img
MOUNT_DIR=~/Embedded_study/mount_root
DRV_FILE=xxx_drv.ko  # 改成你的 .ko 名
APP_FILE=xxx_test    # 改成你的应用名
TARGET_DIR=root/drivers

echo "--- 1. Mounting SD Card ---"
# 自动查找空闲 loop 设备并挂载
LOOP_DEV=$(sudo losetup -P -f --show $SD_IMG)
mkdir -p $MOUNT_DIR
sudo mount ${LOOP_DEV}p2 $MOUNT_DIR

echo "--- 2. Copying Files ---"
# 强制覆盖
sudo cp -v $DRV_FILE $MOUNT_DIR/$TARGET_DIR/
sudo cp -v $APP_FILE $MOUNT_DIR/$TARGET_DIR/

echo "--- 3. Unmounting ---"
sudo umount $MOUNT_DIR
sudo losetup -d $LOOP_DEV

echo "--- Done! Ready to boot QEMU. ---"
```

---

### ✅ 总结：以后你要做什么？

当你想写一个**新的**“控制蜂鸣器”的驱动时：

1. **复制文件夹**：把这套模板复制一份，改名为 `driver_buzzer`。
2. **重命名**：把 `xxx_drv.c` 改为 `buzzer_drv.c`，Makefile 里改一下名字。
3. **填空**：
   * 在 `buzzer_drv.c` 里把 `DRIVER_NAME` 改成 `"lemon_buzzer"`。
   * 把 `COMPATIBLE` 改成 `"lemon,buzzer-v1"`。
   * 在 `write` 函数里填入控制 GPIO 的代码。
4. **改设备树**：打开内核 DTS，加入节点，编译 DTB。
5. **一键运行**：`make` -> `./deploy.sh` -> 启动 QEMU。

这就是专业工程师的工作流。收藏好这套模板！
