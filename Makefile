# SPDX-License-Identifier: GPL-2.0
KERN_DIR = /usr/src/linux-headers-4.15.0-213-generic

all:
	make -C $(KERN_DIR) M=`pwd` modules 

clean:
	make -C $(KERN_DIR) M=`pwd` modules clean
	rm -rf modules.order

up3d610-objs := up3d_core.o up3d_ioctl.o up3d_vb2ops.o up3d_v4l2_fops.o

obj-$(CONFIG_VIDEO_VIVID) += up3d610.o
