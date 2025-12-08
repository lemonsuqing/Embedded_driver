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