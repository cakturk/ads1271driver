GOBUILD = GOARCH=$(GOARCH) GOOS=$(GOOS) go build
GOFLAGS := -ldflags="-s -w"
PROGRAM := ioctl/ioctl
SRC := spidrv.go
GOOS ?= linux
GOARCH ?= arm
BINS := $(PROGRAM) rperiodic.ko

-include .deploy.mk

ifneq ($(KERN_ARCH),)
    ifneq ($(KERN_CROSS_COMPILE),)
    KERN_CROSS=ARCH=$(KERN_ARCH) CROSS_COMPILE=$(KERN_CROSS_COMPILE)
    endif
endif

all: $(PROGRAM)

$(PROGRAM): ioctl/ioctl.go
	@cd ioctl; $(GOBUILD) $(GOFLAGS)

rebuild:
	@cd ioctl; $(GOBUILD) -a $(GOFLAGS)

rsync: $(PROGRAM)
	rsync -vrzh -e "ssh -p $(PORT)" $(CURDIR)/* olimex@$(IPADDR):~/src/ads1271driver

realclean:
	@-rm -f $(PROGRAM)

.PHONY: install rsync clean spidrv rebuild
