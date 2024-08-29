# SPDX-License-Identifier: GPL-2.0
KERN_DIR = /usr/src/linux-headers-4.15.0-213-generic

all:
	make -C $(KERN_DIR) M=`pwd` modules 

clean:
	make -C $(KERN_DIR) M=`pwd` modules clean
	rm -rf modules.order

myvivid-objs := my_vivid-core.o 

#obj-$(CONFIG_VIDEO_VIVID) += vivid.o
obj-$(CONFIG_VIDEO_VIVID) += myvivid.o
