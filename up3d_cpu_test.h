#ifndef __UP3D_CPU_TEST_H__
#define __UP3D_CPU_TEST_H__

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <linux/mm.h>
#include <linux/ioctl.h>
#include <linux/types.h>


#define UP3D_CAMERA_IOCTL_MAGIC  'V'
#define UP3D_CAMERA_GET_BUF_INFO   _IOR(UP3D_CAMERA_IOCTL_MAGIC, 0, struct up3d_camera_buf_info)
#define UP3D_CAMERA_SET_COMPLETED  _IOW(UP3D_CAMERA_IOCTL_MAGIC, 1, __u32)

struct up3d_camera_buf_info {
	__u64 phys_addr;      /* CPU 物理地址，如 0x8a100000 */
	__u32 size;           /* buffer 总大小 */
	__u32 frame_size;     /* 单帧大小，如 832*608 */
};

struct up3d_cpu_test_dev_t {
	struct gpio_desc *completed_gpio;
	
	// reserved memory
	struct up3d_camera_buf_info buf_info;
	struct miscdevice miscdev;
};


int up3d_cpu_test_init(struct platform_device *pdev);
void up3d_cpu_test_exit(struct platform_device *pdev);

#endif