#ifndef __UP3DTECH_610_H__
#define __UP3DTECH_610_H__

#include <media/videobuf2-vmalloc.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-device.h>
#include <linux/printk.h>
#include <linux/kernel.h>

#define WIDTH_MAX	1920
#define HEIGHT_MAX	1080

#define WIDTH_DEF	640
#define HEIGHT_DEF	360

#define MAX_IMAGE_BUFFER_COUNT	16

struct up3d_vb2_buf {
	struct vb2_v4l2_buffer vb;	// 必须在第一个
	bool			prepared;
	struct list_head list;
};

struct up3d_framesize
{
	uint32_t	width;
	uint32_t	height;
};

struct up3d_fmtdesc
{
	uint8_t		description[32]; 	
	uint32_t	pixel_format;		
	uint8_t		bytes_per_pixel;	
	struct up3d_framesize framesize;
};

struct up3d_video_ctx
{
	struct device			*dev;
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

	/* querycap信息 */
	struct v4l2_capability cap;

	uint32_t	width_max;
	uint32_t	height_max;
	uint32_t	width_def;
	uint32_t	height_def;

	int 		irq;
	void 		*ddr_addr;
	uint8_t 	*img_addrs[MAX_IMAGE_BUFFER_COUNT];
	int 		img_blk_count;
	int 		img_index;

	/* controls */
	int 						input_brightness;
};


extern struct up3d_fmtdesc up3d_fmtdesc_lists[];

#endif /*__UP3DTECH_610_H__*/