此实验属于个人项目，其的前提是需要完成下列任务：

# 📘 嵌入式 Linux 从零构建全栈手册 (基于 Vexpress-A9)

**目录索引：**

1. **环境搭建**：地基与工具链
2. **U-Boot 移植**：编译、裁剪与设备树命令开启
3. **Linux 内核**：源码下载、配置、编译与“防卡死”处理
4. **根文件系统**：BusyBox 静态编译与 Init 脚本
5. **应用开发**：编写 C 程序并植入系统
6. **SD 卡持久化**：分区、格式化与脱机运行

---

## 第零章：环境搭建 (一切的开始)

我们要在一个 x86 的 PC (Ubuntu) 上，生成能跑在 ARM 开发板上的代码。

### 0.1 安装基础工具

打开 Ubuntu 终端，执行：

```bash
# 1. 更新软件源
sudo apt update

# 2. 安装构建工具 (Make, GCC, git, 解压工具等)
sudo apt install build-essential libncurses-dev bison flex libssl-dev libelf-dev u-boot-tools qemu-system-arm git wget

# 3. 安装交叉编译器 (核心工具)
# 作用：把 C 代码翻译成 ARM 架构的汇编/机器码
sudo apt install gcc-arm-linux-gnueabihf
```

### 0.2 创建工作目录结构

为了防止文件乱放，我们需要统一规划。

```bash
# 回到家目录
cd ~

# 创建主工作目录
mkdir Embedded_study
cd Embedded_study

# 创建 TFTP 服务器目录 (用于后续网络传输实验)
# 后面 QEMU 会把这个目录映射为网络服务器的根目录
mkdir ~/tftp_root
```

---

## 第一章：Bootloader (U-Boot)

U-Boot 是系统的“第一棒”。它的核心任务是初始化 DDR 内存，并将内核搬运到内存中。

### 1.1 下载源码

```bash
cd ~/Embedded_study
# 克隆 U-Boot 官方仓库
git clone https://github.com/u-boot/u-boot.git
cd u-boot

# 切换到一个稳定的版本 (我们实验用的版本)
git checkout v2023.07
```

### 1.2 配置与裁剪 (关键步骤)

我们需要开启 `fdt` 命令，否则后续无法查看和操作设备树。

1. **应用板级默认配置**：

   ```bash
   # 指定架构和编译器
   export ARCH=arm
   export CROSS_COMPILE=arm-linux-gnueabihf-
   
   # 加载 Vexpress 开发板配置
   make vexpress_ca9x4_defconfig
   ```

2. **强制开启 fdt 命令** (最稳妥的方法)：

   * 直接修改配置文件，防止在 Menuconfig 里找不着。

   ```bash
   # 打开配置文件
   nano .config
   ```

   * 按 `Ctrl+W` 搜索 `CONFIG_CMD_FDT`。

   * 如果找不到，直接滑到文件**最后一行**，新起一行输入：

     ```text
     CONFIG_CMD_FDT=y
     ```

   * 按 `Ctrl+O` 回车保存，`Ctrl+X` 退出。

### 1.3 编译 U-Boot

```bash
# -j8 表示用 8 个线程编译，速度快
make -j8
```

* **产物**：编译完成后，当前目录下会生成 `u-boot` (带调试信息) 和 `u-boot.bin` (二进制)。

### 1.4 启动与设备树实验 (核心原理)

我们要在 U-Boot 里验证它是否能读取设备树。

1. **启动 QEMU**：

   ```bash
   qemu-system-arm -M vexpress-a9 -m 512M -nographic -kernel u-boot
   ```

2. **寻找设备树地址 (原理)**：
   U-Boot 启动时会把内置的设备树（dtb）放在内存的某个位置。我们需要找到它。
   在 `=>` 提示符下输入：

   ```bash
   bdinfo
   ```

   * **现象**：输出中有一行 `fdt_blob` 或 `fdt address`，记录后面的十六进制地址（例如 `0x608xxxxx` 或 `0x7fxxxxxx`）。

3. **挂载设备树**：
   假设你上一步看到的地址是 `0x6087d8b0` (请替换为你实际看到的)：

   ```bash
   fdt addr 0x6087d8b0
   ```

   * **原理**：告诉 fdt 命令工具，“去这个内存地址解析数据”。

4. **查看内容**：

   ```bash
   fdt print /
   ```

   * **现象**：屏幕打印出类似于 JSON 的树状结构。
   * **意义**：证明 U-Boot 功能正常，且具备解析硬件描述的能力。

5. **退出 QEMU**：按 `Ctrl+A`，松开，按 `x`。

---

## 第二章：Linux 内核 (Kernel)

内核是系统的“大脑”。我们要下载源码，并解决两个著名的坑：“指令集不支持”和“启动卡死”。

### 2.1 下载源码 (v5.15)

```bash
cd ~/Embedded_study

# 使用清华源加速下载 (推荐)，或者直接用 kernel.org
# --depth=1 表示只下载最近的提交，省时间省流量
git clone --depth=1 -b v5.15 https://mirrors.tuna.tsinghua.edu.cn/git/linux.git linux-5.15

cd linux-5.15
```

### 2.2 配置内核

我们需要开启 **Early Printk**，否则内核启动时如果出错，会直接卡死且没有任何打印（沉默死亡）。

1. **生成默认配置**：

   ```bash
   # 显式指定架构，防止环境变量失效
   make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- vexpress_defconfig
   ```

2. **进入菜单开启调试**：

   ```bash
   make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- menuconfig
   ```

   * **操作路径**：
     1. `Kernel hacking` --->
     2. `Kernel low-level debugging functions` (按 Y 选中)
     3. `Early printk` (按 Y 选中)
   * 保存并退出。

### 2.3 编译内核 (防坑指南)

Ubuntu 新版 GCC 可能会把代码编译成旧版 ARM 指令，导致报错。我们必须强制指定指令集。

```bash
# zImage: 压缩内核
# dtbs: 编译设备树
# KCFLAGS: 强制指定 ARMv7-A 架构
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- zImage dtbs -j8 KCFLAGS="-march=armv7-a"
```

* **产物**：
  * 内核：`arch/arm/boot/zImage`
  * 设备树：`arch/arm/boot/dts/vexpress-v2p-ca9.dtb`

---

## 第三章：根文件系统 (RootFS)

内核启动后需要运行程序，这些程序存放在文件系统里。我们用 BusyBox 制作一个最小系统。

### 3.1 下载与解压

```bash
cd ~/Embedded_study
# 下载源码
wget https://busybox.net/downloads/busybox-1.36.1.tar.bz2
# 解压
tar -jxvf busybox-1.36.1.tar.bz2
cd busybox-1.36.1
```

### 3.2 静态编译配置 (至关重要)

如果不静态编译，程序在板子上运行时会找不到库文件（Error -2）。

1. **打开配置菜单**：

   ```bash
   make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- menuconfig
   ```

2. **设置静态链接**：

   * 进入 `Settings` --->
   * 找到 `Build static binary (no shared libs)`，按 `Y` 选中变成 `[*]`。
   * 保存退出。

### 3.3 编译与安装

```bash
# 编译
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -j8
# 安装 (生成 _install 目录)
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- install
```

### 3.4 完善文件系统结构

```bash
cd _install
# 创建系统必须的目录
mkdir -p dev proc sys tmp var etc home root
```

### 3.5 编写 Init 脚本 (PID 1)

内核启动的第一个程序。

```bash
# 创建 init 文件
nano init
```

**填入以下内容**：

```bash
#!/bin/sh
# 挂载虚拟文件系统
mount -t proc none /proc
mount -t sysfs none /sys
mount -t tmpfs none /tmp

# 打印信息
echo "-----------------------------------"
echo "  Welcome to My Embedded Linux!"
echo "-----------------------------------"

# 启动 Shell，永不退出
exec /bin/sh
```

**赋予权限**：

```bash
chmod +x init
```

---

## 第四章：应用开发 (用户程序)

我们要写一个 C 程序，让它开机自动运行。

### 4.1 编写与交叉编译

```bash
cd ~
# 创建 main.c
nano main.c
```

**内容**：

```c
#include <stdio.h>
int main() {
    printf("\n[APP] Hello from LemonSuqing's Application!\n\n");
    return 0;
}
```

**编译** (必须静态)：

```bash
arm-linux-gnueabihf-gcc main.c -o lemon_app -static
```

### 4.2 植入文件系统

把这个程序放到我们刚才做的 RootFS 里。

```bash
cp ~/main.c ~/Embedded_study/busybox-1.36.1/_install/bin/lemon_app
# 修正：应该是 cp ~/lemon_app ... 
cp ~/lemon_app ~/Embedded_study/busybox-1.36.1/_install/bin/
```

### 4.3 修改 Init 脚本调用它

修改 `~/Embedded_study/busybox-1.36.1/_install/init`，在 `exec /bin/sh` 之前加入：

```bash
/bin/lemon_app
```

---

## 第五章：打包与网络启动 (集成测试)

### 5.1 制作 uRamdisk (RootFS 镜像)

U-Boot 需要特定格式的文件系统镜像。

```bash
cd ~/Embedded_study/busybox-1.36.1/_install

# 1. 归档并压缩
find . | cpio -o --format=newc > ../rootfs.img
gzip -c ../rootfs.img > ../rootfs.img.gz

# 2. 加 U-Boot 头 (mkimage)
# 产出文件放到 tftp 目录以便下载
mkimage -A arm -O linux -T ramdisk -C gzip -n "RootFS" -d ../rootfs.img.gz ~/tftp_root/uRamdisk
```

### 5.2 搬运内核与设备树

把编译好的内核和设备树也放到 TFTP 目录。

```bash
cd ~/Embedded_study
cp linux-5.15/arch/arm/boot/zImage ~/tftp_root/
cp linux-5.15/arch/arm/boot/dts/vexpress-v2p-ca9.dtb ~/tftp_root/
```

**检查点**：`ls -l ~/tftp_root/` 应该有 `zImage`, `vexpress-v2p-ca9.dtb`, `uRamdisk` 三个文件。

### 5.3 启动 QEMU (网络模式)

```bash
cd ~/Embedded_study/uboot_study/u-boot

# 注意：tftp 路径必须用绝对路径
qemu-system-arm \
    -M vexpress-a9 \
    -m 512M \
    -nographic \
    -kernel u-boot \
    -net nic \
    -net user,tftp=/home/lemonsuqing/tftp_root
```

### 5.4 U-Boot 启动命令

在 U-Boot `=>` 下依次输入：

1. **下载文件** (地址要隔开，防止覆盖)：

   ```bash
   tftp 0x60100000 zImage
   tftp 0x61000000 uRamdisk
   tftp 0x62000000 vexpress-v2p-ca9.dtb
   ```

2. **设置启动参数** (告诉内核根文件系统在内存 ram0)：

   ```bash
   setenv bootargs 'console=ttyAMA0,115200n8 debug earlyprintk root=/dev/ram0 rdinit=/init'
   ```

3. **启动**：

   ```bash
   bootz 0x60100000 0x61000000 0x62000000
   ```

**成功标志**：看到 `Welcome to My Embedded Linux!` 和 `[APP] Hello...` 的打印。

---

## 第六章：SD 卡持久化 (最终形态)

网络启动断电数据就丢了。我们要把系统装进“虚拟 SD 卡”。

### 6.1 制作 SD 卡镜像

退出 QEMU，回到 Ubuntu。

1. **创建空文件 (512MB)**：

   ```bash
   cd ~/Embedded_study
   dd if=/dev/zero of=sd.img bs=1M count=512
   ```

2. **分区 (fdisk)**：

   ```bash
   fdisk sd.img
   ```

   * 输入 `n` -> `p` -> `1` -> `2048` -> `+64M` (创建分区1)
   * 输入 `n` -> `p` -> `2` -> `回车` -> `回车` (创建分区2)
   * 输入 `t` -> `1` -> `c` (修改分区1类型为 FAT32)
   * 输入 `w` (保存)

3. **格式化**：

   ```bash
   # 关联回环设备
   sudo losetup -f --show -P sd.img
   # 假设输出 /dev/loop0
   
   # 格式化 P1 (Boot, FAT32)
   sudo mkfs.vfat -F 32 -n "BOOT" /dev/loop0p1
   # 格式化 P2 (RootFS, EXT4)
   sudo mkfs.ext4 -L "ROOTFS" /dev/loop0p2
   ```

### 6.2 烧写文件

```bash
# 挂载
mkdir -p mount_boot mount_root
sudo mount /dev/loop0p1 mount_boot
sudo mount /dev/loop0p2 mount_root

# 复制内核与设备树到 Boot 分区
sudo cp linux-5.15/arch/arm/boot/zImage mount_boot/
sudo cp linux-5.15/arch/arm/boot/dts/vexpress-v2p-ca9.dtb mount_boot/

# 复制文件系统到 RootFS 分区 (解压状态)
# 注意：这里不是复制 uRamdisk，而是复制 _install 目录下的原始文件
sudo cp -a busybox-1.36.1/_install/* mount_root/

# 修正权限 (必须是 root)
sudo chown -R root:root mount_root/

# 卸载
sudo umount mount_boot mount_root
sudo losetup -d /dev/loop0
```

### 6.3 SD 卡启动验证

1. **启动 QEMU (插入 SD 卡)**：

   ```bash
   cd ~/Embedded_study/uboot_study/u-boot
   qemu-system-arm \
       -M vexpress-a9 \
       -m 512M \
       -nographic \
       -kernel u-boot \
       -sd ~/Embedded_study/sd.img
   ```

2. **U-Boot 操作**：

   ```bash
   # 1. 初始化 MMC
   mmc rescan
   
   # 2. 从 SD 卡读取文件到内存
   # load mmc <设备>:<分区> <地址> <文件名>
   load mmc 0:1 0x60100000 zImage
   load mmc 0:1 0x62000000 vexpress-v2p-ca9.dtb
   
   # 3. 设置启动参数 (关键修改)
   # root=/dev/mmcblk0p2: 告诉内核根文件系统在 SD 卡第 2 分区
   # rootfstype=ext4: 文件系统类型
   # rw: 可读写
   setenv bootargs 'console=ttyAMA0,115200n8 debug earlyprintk root=/dev/mmcblk0p2 rootfstype=ext4 rw init=/init'
   
   # 4. 启动 (中间没有 Ramdisk 了，用 - 代替)
   bootz 0x60100000 - 0x62000000
   ```

3. **持久化验证**：
   进入系统后，执行：

   ```bash
   echo "Data Saved!" > /root/test.txt
   ```

   强制重启 QEMU，再次进入系统，执行 `cat /root/test.txt`，如果内容还在，说明全栈开发实战圆满成功！