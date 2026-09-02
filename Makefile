KDIR := /home/gujun/workspace/driver_learn/source/linux
# 目标架构上运行
ARCH := arm64
# 交叉编译工具链前缀
CROSS_COMPILE := aarch64-linux-gnu-

obj-m += hello.o hello_circul.o timer.o tasklets.o
EXTRA_CFLAGS += -g -O0

all:
	$(MAKE) -C $(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURDIR) KBUILD_EXTRA_SYMBOLS=$(KDIR)/vmlinux.symvers modules

clean:
	$(MAKE) -C $(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURDIR) clean

test:
	$(CROSS_COMPILE)gcc -g -O2 -Wall -o test test_circul.c -lpthread

.PHONY: all clean
