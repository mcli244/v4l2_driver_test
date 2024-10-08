#ifndef __UP3DTECH_610_H__
#define __UP3DTECH_610_H__

#include <media/videobuf2-vmalloc.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-device.h>
#include <linux/printk.h>
#include <linux/kernel.h>

// 默认格式
#define WIDTH_MAX	1920
#define HEIGHT_MAX	1080

#define WIDTH_DEF	640
#define HEIGHT_DEF	360

extern unsigned long long get_current_timestamp(void);

#if 1
#define trace_in()					printk(KERN_DEBUG "%s:%d|%s(%lld) in.", __FILE__, __LINE__,__FUNCTION__, get_current_timestamp())
#define trace_exit()				printk(KERN_DEBUG "%s:%d|%s(%lld) exit.", __FILE__, __LINE__,__FUNCTION__, get_current_timestamp())
#define UP3D_DEBUG(format, ...)  	printk(KERN_DEBUG "%s:%d|%s(%lld) " format , __FILE__, __LINE__,__FUNCTION__, get_current_timestamp(), ##__VA_ARGS__)
#else 
#define trace_in()					
#define trace_exit()				
#define UP3D_DEBUG(format, ...)  	
#endif


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
	uint32_t	pixel_format;		// 像素格式V4L2_PIX_FMT_XXX
	uint8_t		bytes_per_pixel;		// 每个像素占用多少字节
	struct up3d_framesize framesize;
};

struct up3d_video_ctx
{
	struct device			*dev;
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