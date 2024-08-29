#ifndef __UP3DTECH_610_H__
#define __UP3DTECH_610_H__

#include <media/videobuf2-vmalloc.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-device.h>
#include <linux/printk.h>
#include <linux/kernel.h>


#define trace_in()		printk(KERN_WARNING "%s:%d|%s in.", __FILE__, __LINE__,__FUNCTION__)
#define trace_exit()	printk(KERN_WARNING "%s:%d|%s in.", __FILE__, __LINE__,__FUNCTION__)

struct up3d_vb2_buf {
	struct vb2_v4l2_buffer vb;	// 必须在第一个
	struct list_head list;
};

struct up3d_fmtdesc
{
	uint8_t		description[32]; 	
	uint32_t	pixel_format;		// 像素格式V4L2_PIX_FMT_XXX
	uint8_t		bytes_per_pixel;		// 每个像素占用多少字节
};

struct up3d_video_ctx
{
	struct v4l2_format 		cur_v4l2_format;	// 保存当前的格式设置
	struct v4l2_device		v4l2_dev;		
	struct video_device		vid_cap_dev;	
	struct mutex			mutex;
	struct up3d_fmtdesc 	*fmt_lists;			// 支持的格式列表
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
};


extern struct up3d_fmtdesc up3d_fmtdesc_lists[];

#endif /*__UP3DTECH_610_H__*/