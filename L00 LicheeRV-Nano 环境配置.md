# 📘 LicheeRV-Nano (SG2002/RISC-V) 驱动开发全栈手册

本手册基于 **全新 VM (Ubuntu 22.04 LTS)** 环境，旨在部署一个可用于驱动开发的 LicheeRV-Nano 系统。

## 阶段零：环境准备与依赖安装

### 0.1 虚拟机系统初始化

```bash
# 1. 更新与安装基础构建工具
sudo apt update
sudo apt install build-essential libncurses-dev bison flex libssl-dev libelf-dev u-boot-tools git wget cpio python3-pip unzip -y

# 2. 安装 RISC-V 64位交叉编译器
sudo apt install crossbuild-essential-riscv64 -y

# 3. 安装 MTOOLS (解决 RootFS 打包错误)
sudo apt install mtools -y

# 4. 创建工作目录
cd ~
mkdir -p LicheeRV_Nano_Study
cd LicheeRV_Nano_Study
mkdir -p drivers
```

## 阶段一：Sipeed SDK 首次构建与配置修正

### 1.1 获取 Sipeed 构建仓库

```bash
cd ~/LicheeRV_Nano_Study
git clone https://github.com/sipeed/LicheeRV-Nano-Build --depth=1
cd LicheeRV-Nano-Build

# 获取 host-tools (假设已通过浏览器/其他方式下载并解压到此目录)
# 确保 host-tools 目录存在于 LicheeRV-Nano-Build/ 目录下
# git clone https://github.com/sophgo/host-tools --depth=1
```

### 1.2 修复 RootFS 分区大小 (解决溢出错误)

这是上一次构建失败的关键原因。

```bash
# 修改分区文件：build/boards/sg200x/sg2002_licheervnano_sd/partition/partition_sd.xml
nano build/boards/sg200x/sg2002_licheervnano_sd/partition/partition_sd.xml
```

**修改内容：** 将 RootFS 分区大小从 1.6GB 扩大到 2GB。

```xml
<physical_partition type="sd">
    <partition label="BOOT" size_in_kb="80960" readonly="false" file="boot.sd"/>
    <!-- 原始值 1638400KB，修改为 2097152KB (2GB) -->
    <partition label="ROOTFS" size_in_kb="2097152" readonly="false" file="rootfs.sd" /> 
</physical_partition>
```

### 1.3 首次执行完整构建

```bash
# 1. 初始化环境
source build/cvisetup.sh

# 2. 设置编译目标 (C906核心，SD卡启动)
defconfig sg2002_licheervnano_sd

# 3. 执行首次构建
build_all
# 等待构建完成，生成最终的 .img 文件。
```
