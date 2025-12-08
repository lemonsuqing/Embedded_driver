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