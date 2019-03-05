#ifndef _SPI_SPP_H
#define _SPI_SPP_H

#include <linux/types.h>
#include <linux/ioctl.h>

struct spp_conf {
	__s64 secs;
	__u64 nsecs;
};

#define SPP_MAGIC '\\'

#define SPPIOC_START    _IO(SPP_MAGIC, 1)
#define SPPIOC_STOP     _IO(SPP_MAGIC, 2)
#define SPPIOC_SPARAMS  _IOW(SPP_MAGIC, 3, struct spp_conf)

#endif
