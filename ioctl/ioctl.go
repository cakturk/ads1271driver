package main

import (
	"fmt"
	"log"
	"os"
	"syscall"
)

func ioctl(f uintptr, op uint, arg uintptr) error {
	r1, _, err := syscall.Syscall(syscall.SYS_IOCTL, f, uintptr(op), arg)
	if err != 0 {
		return err
	}
	if r1 != 0 {
		return syscall.Errno(r1)
	}
	return nil
}

type sppConf struct {
	secs  int64
	nsecs uint64
}

const (
	spiPeriodic = "/dev/spiperiodic"

	// ioctl requests
	sppIocSConf = 0x40105c03
	sppIocStart = 0x5c01
	sppIocStop  = 0x5c02
)

func main() {
	f, err := os.Open(spiPeriodic)
	if err != nil {
		log.Fatal(err)
	}
	spp := sppConf{
		secs:  0,
		nsecs: 500000000,
	}
	_ = spp
	// err = ioctl(f.Fd(), sppIocSConf, uintptr(unsafe.Pointer(&spp)))
	// err = ioctl(f.Fd(), sppIocStart, 0)
	err = ioctl(f.Fd(), sppIocStop, 0)
	if err != nil {
		log.Fatal(err)
	}
	fmt.Println("Done!")
}
