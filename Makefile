KDIR := /home/gujun/workspace/driver_learn/source/linux
# 目标架构上运行
ARCH := arm64
# 交叉编译工具链前缀
CROSS_COMPILE := aarch64-linux-gnu-

obj-m += hello.o
EXTRA_CFLAGS += -g -O0

all:
	$(MAKE) -C $(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURDIR) KBUILD_EXTRA_SYMBOLS=$(KDIR)/vmlinux.symvers modules

clean:
	$(MAKE) -C $(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURDIR) clean

.PHONY: all clean
