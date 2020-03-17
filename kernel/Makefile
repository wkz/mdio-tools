obj-m := mdio-netlink.o
ccflags-y := -I$(src)/../include

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

install: all
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules_install

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
