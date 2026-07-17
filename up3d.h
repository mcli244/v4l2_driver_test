#ifndef __UP3DTECH_610_H__
#define __UP3DTECH_610_H__

#include <media/videobuf2-vmalloc.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-device.h>
#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/miscdevice.h>

// 最大分辨率
#define WIDTH_MAX	4096
#define HEIGHT_MAX	2160

#define WIDTH_DEF	(832)
#define HEIGHT_DEF	(608*3)

#define UP3D_STA_STOP 0
#define UP3D_STA_RUN 1
#define UP3D_STA_PAUSE 2

#define VIDIOC_UP3D_GET_STATUS  _IOR('V', 610, struct up3d_device_info)
struct up3d_device_info {
	atomic_t 	irq_count;
	uint16_t 	vb_free;
	uint16_t 	vb_free_min;
	struct v4l2_format 		cur_v4l2_format;

	uint32_t	irq_interval_time_ms;
	uint32_t	buf_queue_interval_time_ms;
	uint32_t	fpga_enable_interval_time_ms;
	uint32_t 	fpga_discarded_frames_cnt;
};


struct up3d_vb2_buf {
	struct vb2_v4l2_buffer vb;	// 必须在第一个
	bool			prepared;
	struct list_head list;
};

struct up3d_framesize
{
	uint32_t	width;
	uint32_t	height;
	uint32_t	bytes_per_pixel;
};

struct up3d_fmtdesc
{
	uint8_t		description[128]; 	
	uint32_t	pixel_format;		
	uint8_t		bytes_per_pixel;	
	struct up3d_framesize framesize;
};

struct up3d_video_ctx
{
	struct device			*dev;
	struct platform_device 	*pdev;
	struct v4l2_format 		cur_v4l2_format;	
	struct v4l2_device		v4l2_dev;		
	struct video_device		vid_cap_dev;	
	struct mutex			mutex;
	struct up3d_fmtdesc 	*fmt_lists;			
	uint32_t 				fmt_lists_cnt;		

	/* 队列和buffer */
	struct vb2_queue vb_queue;
	struct list_head vb_queue_active;
	spinlock_t		 vb_queue_lock;
	struct work_struct irq_work;
	struct up3d_vb2_buf *current_vb;

	/* querycap信息 */
	struct v4l2_capability cap;

	int 		irq;

	/* controls */
	int 						input_brightness;
	struct up3d_device_info 	device_info;
	struct dentry 				*debugfs_root;


	/* FPGA相关 */
	void *fpga_base_addr;
	struct miscdevice fpga_miscdev;	// 算法参数调整、测试图mmap的设备节点

	// 测试图相关
	struct up3d_framesize input_image;
	size_t input_image_buffer_size;
	struct up3d_framesize output_images[3];	// left, right, rgb
	struct gpio_desc *completed_gpio;
	struct timer_list key_debounce_timer;

	bool is_capturing;
	bool is_streaming;
};

#endif /*__UP3DTECH_610_H__*/