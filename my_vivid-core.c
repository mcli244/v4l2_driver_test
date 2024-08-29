/*
 * vivid-core.c - A Virtual Video Test Driver, core initialization
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/font.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/videodev2.h>
#include <linux/v4l2-dv-timings.h>
#include <media/videobuf2-vmalloc.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>
#include <linux/timer.h>

#include "vivid-core.h"
#include "vivid-vid-common.h"
#include "vivid-vid-cap.h"
#include "vivid-vid-out.h"
#include "vivid-radio-common.h"
#include "vivid-radio-rx.h"
#include "vivid-radio-tx.h"
#include "vivid-sdr-cap.h"
#include "vivid-vbi-cap.h"
#include "vivid-vbi-out.h"
#include "vivid-osd.h"
#include "vivid-cec.h"
#include "vivid-ctrls.h"

#define trace_in()	printk(KERN_WARNING "%s:%d|%s in.", __FILE__, __LINE__,__FUNCTION__)
#define trace_exit()	printk(KERN_WARNING "%s:%d|%s in.", __FILE__, __LINE__,__FUNCTION__)

#define VIVID_MODULE_NAME "myvivid"

// 默认格式
#define WIDTH_MAX	1920
#define HEIGHT_MAX	1080

#define WIDTH_DEF	640
#define HEIGHT_DEF	360

struct my_vb2_buf {
	struct vb2_v4l2_buffer vb;	// 必须在第一个
	struct list_head list;
};

typedef struct 
{
	uint8_t		description[32]; 	
	uint32_t	pixel_format;		// 像素格式V4L2_PIX_FMT_XXX
	uint8_t		bytes_per_pixel;		// 每个像素占用多少字节
}my_fmtdesc_t;

struct myvideo_ctx_t{
	struct v4l2_format 		cur_v4l2_format;	// 保存当前的格式设置
	struct v4l2_device		v4l2_dev;		
	struct video_device		vid_cap_dev;	
	struct mutex			mutex;
	my_fmtdesc_t *fmt_lists;

	// 队列和buffer
	struct vb2_queue my_vb_queue;
	struct list_head my_vb_queue_active;
	spinlock_t		 my_vb_queue_lock;
};

static struct myvideo_ctx_t myvideo_ctx;

static my_fmtdesc_t my_fmtdesc_lists[]=
{
	{
		.description = "8:8:8, RGB",
		.pixel_format = V4L2_PIX_FMT_RGB24,
		.bytes_per_pixel = 3,
	},
	{
		.description = "5:6:5, RGB",
		.pixel_format = V4L2_PIX_FMT_RGB565,
		.bytes_per_pixel = 1,
	},
	{
		.description = "16  YUV 4:2:2",
		.pixel_format = V4L2_PIX_FMT_YUYV,
		.bytes_per_pixel = 1,
	}
};

static struct timer_list myvivi_timer;

static int my_vidioc_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	printk(KERN_WARNING "my_vidioc_querycap.");
    strcpy(cap->driver, "my_vdev"); // 驱动名称
	strcpy(cap->card, "my_vdev");   // 设备名称
	cap->version = 0x0001;          // 版本号
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;    // 能力，捕获和流 
	
  /*     物理设备 整体功能
  * @capabilities: capabilities of the physical device as a whole
    通过此特定设备（节点）访问的功能 
  * @device_caps:  capabilities accessed via this particular device (node)
  */
	cap->capabilities |=  V4L2_CAP_DEVICE_CAPS; // 4.15.0内核下需要设置，估计往上的版本也需要置位该标志
	
	return 0;
}

/** 
 * 调用时机：由ioctl命令VIDIOC_REQBUFS和VIDIOC_CREATE_BUFS调用时被调用
 * 作用：设置参数
 */
static int myvivid_queue_setup(struct vb2_queue *q,
			   unsigned int *num_buffers, unsigned int *num_planes,
			   unsigned int sizes[], struct device *alloc_devs[])
{
	struct myvideo_ctx_t *ctx = vb2_get_drv_priv(q);

	trace_in();

	*num_planes = 1;	// 目前只支持单层，设为1
	sizes[0] = ctx->cur_v4l2_format.fmt.pix.sizeimage;
	// TODO:num_buffers\alloc_devs待研究

	trace_exit();

	return 0;
};

/** 
 * 调用时机：缓冲区被放入到队列前调用此函数
 * 作用：动需要执行一些初始化工作或获取、修改缓冲区，若驱动支持VIDIOC_CREATE_BUFS，
 * 		 则需要验证缓冲区的大小，若有错误发生，则缓冲区不会入队。
 */
static int myvivid_buf_prepare(struct vb2_buffer *vb)
{
	struct myvideo_ctx_t *ctx = vb2_get_drv_priv(vb->vb2_queue);
	int ret = 0;

	trace_in();
	// 设置payload，payload为图像大小
	vb2_set_plane_payload(vb, 0, ctx->cur_v4l2_format.fmt.pix.sizeimage);
	// 缓冲区的有效字节数为图像大小
	vb->planes[0].bytesused = 0;	
	// 检查缓冲区虚拟地址是否存在和payload是否正确设置
	if (vb2_plane_vaddr(vb, 0) &&
		vb2_get_plane_payload(vb, 0) > vb2_plane_size(vb, 0)) {
		ret = -EINVAL;
		goto out;
	}
	return 0;

	trace_exit();
out:
	return ret;
}

static void myvivid_buf_finish(struct vb2_buffer *vb)
{
	trace_in();
}

/**  必要
 * 调用时机：
 * 作用：缓冲区加入队列
 */
static void myvivid_buf_queue(struct vb2_buffer *vb)
{
	
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct my_vb2_buf *my_vb = container_of(vbuf, struct my_vb2_buf, vb);
	struct myvideo_ctx_t *ctx = vb2_get_drv_priv(vb->vb2_queue);

	trace_in();

	spin_lock(&ctx->my_vb_queue_lock);
	list_add_tail(&my_vb->list, &ctx->my_vb_queue_active);
	spin_unlock(&ctx->my_vb_queue_lock);

	trace_exit();
}

static int myvivid_start_streaming(struct vb2_queue *q, unsigned int count)
{
	trace_in();

	// TODO:控制硬件开始采集


	return 0;
}

/**  必要
 * 调用时机：
 * 作用：停止请求
 */
static void myvivid_stop_streaming(struct vb2_queue *q)
{
	struct myvideo_ctx_t *ctx = vb2_get_drv_priv(q);
	unsigned long flags;

	trace_in();

	// TODO:控制硬件停止采集
	spin_lock_irqsave(&ctx->my_vb_queue_lock, flags);
	while (!list_empty(&ctx->my_vb_queue_active)) {
		struct my_vb2_buf *buf = list_entry(ctx->my_vb_queue_active.next, struct my_vb2_buf, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&ctx->my_vb_queue_lock, flags);

}

static void myvivid_wait_prepare(struct vb2_queue *q)
{
	trace_in();
}

static void myvivid_wait_finish(struct vb2_queue *q)
{
	trace_in();
}

static int myvivid_buf_init(struct vb2_buffer *vb)
{
	trace_in();
	return 0;
}

static void myvivid_buf_cleanup(struct vb2_buffer *vb)
{
	trace_in();
}

const struct vb2_ops my_vb2_ops = {
	.queue_setup		= myvivid_queue_setup,
	.buf_prepare		= myvivid_buf_prepare,
	.buf_finish			= myvivid_buf_finish,
	.buf_queue			= myvivid_buf_queue,
	.start_streaming	= myvivid_start_streaming,
	.stop_streaming		= myvivid_stop_streaming,
	.wait_prepare		= myvivid_wait_prepare,
	.wait_finish		= myvivid_wait_finish,
	.buf_init			= myvivid_buf_init,
	.buf_cleanup		= myvivid_buf_cleanup,
};

static int _vb_queue_init(struct vb2_queue *q)
{
    q->type 				= V4L2_BUF_TYPE_VIDEO_CAPTURE;  		// 类型是视频捕获设备
    q->io_modes 			= VB2_MMAP; 							// 仅支持应用层做mmap映射的方式
    q->buf_struct_size 		= sizeof(struct my_vb2_buf);
    q->ops 					= &my_vb2_ops,   				
	// 缓存驱对应的内存分配器操作函数，这里vb2_vmalloc_memops不止一种。vb2_dma_contig_memops\vb2_dma_sg_memops\vb2_vmalloc_memops\或者自定义
	// 详细见https://cloud.tencent.com/developer/article/2320146 "缓冲区的I/O模式"
    q->mem_ops 				= &vb2_vmalloc_memops;   				// videobuf2_vmalloc.h
    q->timestamp_flags 		= V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC; 	// 时间戳是线性增加的
    q->min_buffers_needed 	= 2;
    q->lock 				= &myvideo_ctx.mutex;					// 保护struct vb2_queue的互斥锁，使缓冲队列的操作串行化，若驱动实有互斥锁，则可设置为NULL，videobuf2核心层API不使用此锁
    // q->dev 					= my_vivid_dev->v4l2_dev.dev;
	q->drv_priv				= &myvideo_ctx;

	spin_lock_init(&myvideo_ctx.my_vb_queue_lock);
	INIT_LIST_HEAD(&myvideo_ctx.my_vb_queue_active);

    return vb2_queue_init(q);
}


static int _init_format(struct v4l2_format *f)
{
	f->fmt.pix.width = WIDTH_DEF;
	f->fmt.pix.height = HEIGHT_DEF;
	f->fmt.pix.pixelformat = my_fmtdesc_lists[0].pixel_format;
	f->fmt.pix.bytesperline = f->fmt.pix.width * my_fmtdesc_lists[0].bytes_per_pixel;	
	f->fmt.pix.sizeimage = f->fmt.pix.bytesperline * f->fmt.pix.height;
	return 0;
}

static int myvivid_open(struct file *file)
{
	int ret;
	trace_in();

	// 初始化当前的格式
	myvideo_ctx.fmt_lists = &my_fmtdesc_lists[0];
	ret = _init_format(&myvideo_ctx.cur_v4l2_format);

	ret = _vb_queue_init(&myvideo_ctx.my_vb_queue);
	

    return 0;
}

static int myvivid_release(struct file *file)
{
    trace_in();
    return 0;
}

static unsigned int myvivid_poll(struct file *file, struct poll_table_struct *ptable)
{
	trace_in();
    return 0;
}

static int myvivid_mmap(struct file *file, struct vm_area_struct * vma)
{
	trace_in();
    return 0;
}

static const struct v4l2_file_operations myvivid_fops = {
	.owner			= THIS_MODULE,
	.open           = myvivid_open,
	.release        = myvivid_release,
	// .read           = myvivid_read,		// 本驱动只支持MMAP 所以这里不提供read/wirte方式的接口
	// .write          = myvivid_write,
	.poll			= myvivid_poll,
	.mmap           = myvivid_mmap,
	// ioctl -> unlocked_ioctl -> video_ioctl2 -> my_v4l2_ioctl_ops(函数集) 相当于通过video_ioctl2中转了一次
	.unlocked_ioctl = video_ioctl2,		
};

/* 列举支持哪种格式 */
static int myvivi_vidioc_enum_fmt_vid_cap(struct file *file, void *fh,struct v4l2_fmtdesc *f)
{
	printk(KERN_WARNING "%s.", __FUNCTION__);
	if (f->index >= ARRAY_SIZE(my_fmtdesc_lists))	
		return -EINVAL;

	strcpy(f->description, my_fmtdesc_lists[f->index].description);
	f->pixelformat = my_fmtdesc_lists[f->index].pixel_format;

	return 0;
}

/* 获取当前使用的格式 */
static int myvivi_vidioc_g_fmt_vid_cap(struct file *file, void *fh,struct v4l2_format *f)
{
	printk(KERN_WARNING "%s.", __FUNCTION__);
	memcpy(f, &myvideo_ctx.cur_v4l2_format, sizeof(struct v4l2_format));
	return 0;
}


/* 尝试是否支持某种格式 */
static int myvivi_vidioc_try_fmt_vid_cap(struct file *file, void *fh,struct v4l2_format *f)
{
    enum v4l2_field field;
	int index=0;
	
	trace_in();

	for(index=0; index<ARRAY_SIZE(my_fmtdesc_lists); index++)
	{
		if(f->fmt.pix.pixelformat == my_fmtdesc_lists[index].pixel_format)
			break;
	}

	if(index >= ARRAY_SIZE(my_fmtdesc_lists))
		return -EINVAL;
	
	field = f->fmt.pix.field;
	if (field == V4L2_FIELD_ANY) {
		field = V4L2_FIELD_INTERLACED;
	} else if (V4L2_FIELD_INTERLACED != field) {
		return -EINVAL;
	}

	v4l_bound_align_image(&f->fmt.pix.width, 48, WIDTH_MAX, 2,	&f->fmt.pix.height, 32, HEIGHT_MAX, 0, 0);
	f->fmt.pix.bytesperline =	f->fmt.pix.width * my_fmtdesc_lists[index].bytes_per_pixel;
	f->fmt.pix.sizeimage 	=	f->fmt.pix.height * f->fmt.pix.bytesperline;

	return 0;
}

static int myvivi_vidioc_s_fmt_vid_cap(struct file *file, void *fh,struct v4l2_format *f)
{
	int ret;
	printk(KERN_WARNING "%s.", __FUNCTION__);
	ret = myvivi_vidioc_try_fmt_vid_cap(file, NULL, f);
	if (ret < 0)
		return ret;

    memcpy(&myvideo_ctx.cur_v4l2_format, f, sizeof(myvideo_ctx.cur_v4l2_format));
    
	return 0;
}

/* 枚举支持的输入设备 */
#define INPUT_DEVICE_NUMS	1
static int my_vidioc_enum_input(struct file *file, void *fh,struct v4l2_input *inp)
{
	if (inp->index >= INPUT_DEVICE_NUMS)	// 只支持一种输入设备，就是相机自身
		return -EINVAL;	
 
	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->std = V4L2_STD_525_60;
	sprintf(inp->name, "Camera %u", inp->index);
	return 0;
}

/* 获取输入设备 */
static int my_vidioc_g_input(struct file *file, void *fh, unsigned int *i)
{
	struct vivid_dev *dev = video_drvdata(file);
	*i = dev->input;
	return 0;
}

/* 设置输入设备 */
static int my_vidioc_s_input(struct file *file, void *fh, unsigned int i)
{
	struct vivid_dev *dev = video_drvdata(file);
 
	if (i >= INPUT_DEVICE_NUMS)
		return -EINVAL;
 
	dev->input = i;
	dev->vid_cap_dev.tvnorms = V4L2_STD_ALL;
	return 0;
}

const struct v4l2_ioctl_ops my_v4l2_ioctl_ops=
{
    // 表示它是一个摄像头设备
    .vidioc_querycap      = my_vidioc_querycap,

    /* 用于列举、获得、测试、设置摄像头的数据的格式 */
    .vidioc_enum_fmt_vid_cap  = myvivi_vidioc_enum_fmt_vid_cap,
    .vidioc_g_fmt_vid_cap     = myvivi_vidioc_g_fmt_vid_cap,
    .vidioc_try_fmt_vid_cap   = myvivi_vidioc_try_fmt_vid_cap,
    .vidioc_s_fmt_vid_cap     = myvivi_vidioc_s_fmt_vid_cap,
    
    /* 缓冲区操作: 申请/查询/放入队列/取出队列 使用videobuffer2提供的函数 */
	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,

	/* 输入设备相关 */
	.vidioc_enum_input		= my_vidioc_enum_input,
	.vidioc_g_input			= my_vidioc_g_input,
	.vidioc_s_input			= my_vidioc_s_input,

};

static void my_v4l2_release(struct v4l2_device *v4l2_dev)
{

}


static int my_vivid_probe(struct platform_device *pdev)
{
    int erron;
    int ret;
    struct video_device *vfd;

    printk(KERN_WARNING "my_vivid_probe.");
    printk(KERN_WARNING "my_vivid_probe.1");
    /* register v4l2_device */
    snprintf(myvideo_ctx.v4l2_dev.name, sizeof(myvideo_ctx.v4l2_dev.name), "%s-%03d", VIVID_MODULE_NAME, 0);
	ret = v4l2_device_register(&pdev->dev, &myvideo_ctx.v4l2_dev);
	if (ret) {
		return ret;
	}
	myvideo_ctx.v4l2_dev.release = my_v4l2_release;
	printk(KERN_WARNING "my_vivid_probe.123");
	
    // 视频设备操作
	mutex_init(&myvideo_ctx.mutex);
    vfd = &myvideo_ctx.vid_cap_dev;
    snprintf(vfd->name, sizeof(vfd->name),  "vivid-%03d-vid-cap", 0);
    vfd->fops = &myvivid_fops;
    vfd->ioctl_ops = &my_v4l2_ioctl_ops;
    vfd->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
    vfd->release = video_device_release_empty;
    vfd->v4l2_dev = &myvideo_ctx.v4l2_dev;
    vfd->queue = &myvideo_ctx.my_vb_queue;  
    vfd->tvnorms = 0;   // 意义不明
    vfd->lock = &myvideo_ctx.mutex;    // 未v4l2设置锁，先没用上暂时不加
	video_set_drvdata(vfd, &myvideo_ctx);
    erron = video_register_device(vfd, VFL_TYPE_GRABBER, -1);
    if(erron)
    {
        printk(KERN_WARNING "video_register_device erron:%d ", erron);
        goto unreg_dev;
    }

    printk(KERN_WARNING "my_vivid_probe.6");
    return 0;

unreg_dev:

    printk(KERN_WARNING "my_vivid_probe error");
    v4l2_device_put(&myvideo_ctx.v4l2_dev);

    return -ENOMEM;
}
static int my_vivid_remove(struct platform_device *dev)
{
    printk(KERN_WARNING "my_vivid_remove.");

    video_unregister_device(&myvideo_ctx.vid_cap_dev);
    vb2_queue_release(&myvideo_ctx.my_vb_queue);
    v4l2_device_put(&myvideo_ctx.v4l2_dev);

    return 0;
}

static void my_vivid_pdev_release(struct device *dev)
{
}

static struct platform_device my_vivid_pdev = {
	.name		= "my_vivid",
	.dev.release	= my_vivid_pdev_release,
};

static struct platform_driver my_vivid_pdrv = {
	.probe		= my_vivid_probe,
	.remove		= my_vivid_remove,
	.driver		= {
		.name	= "my_vivid",
	},
};

void myvivi_timer_function(struct timer_list *timer)
{
	printk(KERN_WARNING "myvivi_timer_function.");
	mod_timer(timer, jiffies + HZ);

#if 0
	struct videobuf_buffer *vb;
	void *vbuf;
	struct timeval ts;
    
	struct vivid_buffer *buf;

	if (!list_empty(&my_vivid_dev->vid_cap_active)) {
		buf = list_entry(&my_vivid_dev->vid_cap_active,
				struct vivid_buffer, list);
		list_del(&buf->list);
	}

	if(buf)
	{
		myvivid_fillbuff(my_vivid_dev, buf);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}

out:
    /* 3. 修改timer的超时时间 : 30fps, 1秒里有30帧数据
     *    每1/30 秒产生一帧数据
     */
    mod_timer(&myvivi_timer, jiffies + HZ/30);
	#endif

}


static int __init my_vivid_init(void)
{
	int ret;
    printk(KERN_WARNING "my_vivid_init.");

	ret = platform_device_register(&my_vivid_pdev);
	if (ret)
		return ret;

	ret = platform_driver_register(&my_vivid_pdrv);
	if (ret)
		platform_device_unregister(&my_vivid_pdev);
	
	// 测试定时器
	myvivi_timer.expires = jiffies + 5;
	myvivi_timer.function = myvivi_timer_function;
	add_timer(&myvivi_timer);

	return ret;
}

static void __exit my_vivid_exit(void)
{
    printk(KERN_WARNING "my_vivid_exit.");
	platform_driver_unregister(&my_vivid_pdrv);
	platform_device_unregister(&my_vivid_pdev);
}


module_init(my_vivid_init);
module_exit(my_vivid_exit);
MODULE_DESCRIPTION("Video Test Driver");
MODULE_AUTHOR("CoreyLee");
MODULE_LICENSE("GPL");