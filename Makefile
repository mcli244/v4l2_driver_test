# SPDX-License-Identifier: GPL-2.0
KERN_DIR = /home/up3d/polarfire-soc/linux

all:
	make ARCH=riscv CROSS_COMPILE=riscv64-mchp-linux- -C $(KERN_DIR) M=`pwd` modules 

clean:
	make ARCH=riscv CROSS_COMPILE=riscv64-mchp-linux- -C $(KERN_DIR) M=`pwd` modules clean
	rm -rf modules.order
	rm .*.cmd *.mod.*
	rm Module.symvers modules.order

cp:
	cp up800w_v4l2.ko /home/up3d/polarfire-soc/nfs/up3d/

up800w_v4l2-objs := up3d_core.o up3d_ioctl.o up3d_vb2ops.o up3d_sysfs.o up3d_fpga.o

obj-m += up800w_v4l2.o
