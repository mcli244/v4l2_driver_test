# SPDX-License-Identifier: GPL-2.0
KERN_DIR = /home/uisrc/workspace/pack/ext_source/kernel-source

all:
	make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -C $(KERN_DIR) M=`pwd` modules 

clean:
	make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -C $(KERN_DIR) M=`pwd` modules clean
	rm -rf modules.order

up3d610-objs := up3d_core.o up3d_ioctl.o up3d_vb2ops.o up3d_v4l2_fops.o up3d_utils.o

obj-m += up3d610.o
