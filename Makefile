ifeq ($(KERNELRELEASE),)

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

.PHONY: build clean

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

install:
	$(MAKE) INSTALL_MOD_DIR=eudyptula -C $(KERNELDIR) M=$(PWD) modules_install

clean:
	rm -fr *.o *~ core .depend .*.cmd *.ko *.mod.c
	rm -fr modules.order Module.symvers .tmp_versions
else

$(info Building with KERNELRELEASE = ${KERNELRELEASE})
obj-m := rperiodic.o

endif
