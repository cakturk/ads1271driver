ifeq ($(KERNELRELEASE),)

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

.PHONY: build clean

default:
	$(KERN_CROSS) $(MAKE) -C $(KDIR) M=$(PWD) modules

install:
	$(KERN_CROSS) $(MAKE) INSTALL_MOD_DIR=eudyptula -C $(KDIR) M=$(PWD) modules_install

clean:
	rm -fr *.o *~ core .depend .*.cmd *.ko *.mod.c
	rm -fr modules.order Module.symvers .tmp_versions

-include deploy.mk

else

$(info Building with KERNELRELEASE = ${KERNELRELEASE})
obj-m := rperiodic.o

endif
