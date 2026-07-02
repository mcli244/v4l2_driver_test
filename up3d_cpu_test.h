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

// 算法参数寄存器
#define UP3D_CAMERA_ALGO_PARAM_REG_BASE (0x40000500)
#define MIN_CONTRAST_REG 				(0x00)
#define MEAN_OFFSET_REG 				(0x04)
#define HIGH_LEVEL_REG 					(0x08)
#define MIN_PEAK_REG 					(0x0C)
#define MIN_VALUE_REG 					(0x10)
#define JANUS_THRESHOLD_REG 			(0x14)

#define UP3D_CAMERA_IOCTL_MAGIC  'V'
#define UP3D_CAMERA_GET_BUF_INFO   _IOR(UP3D_CAMERA_IOCTL_MAGIC, 0, struct up3d_camera_buf_info)
#define UP3D_CAMERA_SET_COMPLETED  _IOW(UP3D_CAMERA_IOCTL_MAGIC, 1, __u32)
#define UP3D_CAMERA_GET_ALGO_PARAMS   _IOR(UP3D_CAMERA_IOCTL_MAGIC, 2, struct up3d_algo_params_t)
#define UP3D_CAMERA_SET_ALGO_PARAMS   _IOW(UP3D_CAMERA_IOCTL_MAGIC, 3, struct up3d_algo_params_t)

struct up3d_camera_buf_info {
	__u64 phys_addr;      /* CPU 物理地址，如 0x8a100000 */
	__u32 size;           /* buffer 总大小 */
	__u32 frame_size;     /* 单帧大小，如 832*608 */
};

struct up3d_algo_params_t {
	int min_contrast;      /* 最小对比度 */
	int mean_offset;       /* 均值偏移 */
	int high_level;        /* 高电平阈值 */
	int min_peak;          /* 最小峰值 */
	int min_value;         /* 最小值 */
	int janus_threshold;   /* janus 阈值 */
};

struct up3d_cpu_test_dev_t {
	struct device *dev;
	struct gpio_desc *completed_gpio;
	
	// reserved memory
	struct up3d_camera_buf_info buf_info;
	struct miscdevice miscdev;

	// 算法参数寄存器
	void *algo_base_addr;
};


int up3d_cpu_test_init(struct platform_device *pdev);
void up3d_cpu_test_exit(struct platform_device *pdev);
void up3d_cpu_test_stop(void);
void up3d_cpu_test_start(void);
#endif