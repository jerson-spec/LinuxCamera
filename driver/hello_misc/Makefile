obj-m += hello_misc.o

KDIR ?= $(HOME)/100ask_imx6ull-sdk/Linux-4.9.88
ARCH ?= arm
CROSS_COMPILE ?= arm-buildroot-linux-gnueabihf-

all:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules
	$(CROSS_COMPILE)gcc -o test_hello test_hello.c

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -rf test_hello

