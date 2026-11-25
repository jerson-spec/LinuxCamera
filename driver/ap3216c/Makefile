obj-m += ap3216c.o

KDIR ?= $(HOME)/100ask_imx6ull-sdk/Linux-4.9.88
ARCH ?= arm
CROSS_COMPILE ?= arm-buildroot-linux-gnueabihf-

all:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
