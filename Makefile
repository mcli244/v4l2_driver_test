# SPDX-License-Identifier: GPL-2.0
KERN_DIR = /usr/src/linux-headers-4.15.0-213-generic

all:
	make -C $(KERN_DIR) M=`pwd` modules 

clean:
	make -C $(KERN_DIR) M=`pwd` modules clean
	rm -rf modules.order

vivid-objs := vivid-core.o vivid-ctrls.o vivid-vid-common.o vivid-vbi-gen.o \
		vivid-vid-cap.o vivid-vid-out.o vivid-kthread-cap.o vivid-kthread-out.o \
		vivid-radio-rx.o vivid-radio-tx.o vivid-radio-common.o \
		vivid-rds-gen.o vivid-sdr-cap.o vivid-vbi-cap.o vivid-vbi-out.o \
		vivid-osd.o

myvivid-objs := my_vivid-core.o vivid-ctrls.o vivid-vid-common.o vivid-vbi-gen.o \
		vivid-vid-cap.o vivid-vid-out.o vivid-kthread-cap.o vivid-kthread-out.o \
		vivid-radio-rx.o vivid-radio-tx.o vivid-radio-common.o \
		vivid-rds-gen.o vivid-sdr-cap.o vivid-vbi-cap.o vivid-vbi-out.o \
		vivid-osd.o

ifeq ($(CONFIG_VIDEO_VIVID_CEC),y)
  vivid-objs += vivid-cec.o
  myvivid-objs += vivid-cec.o
endif

#obj-$(CONFIG_VIDEO_VIVID) += vivid.o
obj-$(CONFIG_VIDEO_VIVID) += myvivid.o
