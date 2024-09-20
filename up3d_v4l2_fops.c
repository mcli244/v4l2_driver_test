#include "up3d_v4l2_fops.h"
#include "up3d_vb2ops.h"
#include "up3d.h"

#include <linux/videodev2.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>
#include <media/v4l2-ioctl.h>

static int _vb_queue_init(struct vb2_queue *q, struct up3d_video_ctx *ctx)
{
	trace_in();

    q->type 				= V4L2_BUF_TYPE_VIDEO_CAPTURE;  		// 类型是视频捕获设备
    q->io_modes 			= VB2_MMAP; 							// 仅支持应用层做mmap映射的方式
    q->buf_struct_size 		= sizeof(struct up3d_vb2_buf);
    q->ops 					= &up3d_vb2_ops,   				
	// 缓存驱对应的内存分配器操作函数，这里vb2_vmalloc_memops不止一种。vb2_dma_contig_memops\vb2_dma_sg_memops\vb2_vmalloc_memops\或者自定义
	// 详细见https://cloud.tencent.com/developer/article/2320146 "缓冲区的I/O模式"
    q->mem_ops 				= &vb2_vmalloc_memops;   				// videobuf2_vmalloc.h
    q->timestamp_flags 		= V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC; 	// 时间戳是线性增加的
    q->min_buffers_needed 	= 2;
    q->lock 				= &ctx->mutex;					// 保护struct vb2_queue的互斥锁，使缓冲队列的操作串行化，若驱动实有互斥锁，则可设置为NULL，videobuf2核心层API不使用此锁
	q->drv_priv				= ctx;

	spin_lock_init(&ctx->vb_queue_lock);
	INIT_LIST_HEAD(&ctx->vb_queue_active);

	trace_exit();
    return vb2_queue_init(q);
}


static int _init_format(struct v4l2_format *f, struct up3d_video_ctx *ctx)
{
	trace_in();
	f->fmt.pix.width = ctx->width_def;
	f->fmt.pix.height = ctx->height_def;
	f->fmt.pix.pixelformat = ctx->fmt_lists[0].pixel_format;
	f->fmt.pix.bytesperline = f->fmt.pix.width * ctx->fmt_lists[0].bytes_per_pixel;	
	f->fmt.pix.sizeimage = f->fmt.pix.bytesperline * f->fmt.pix.height;
	trace_exit();
	return 0;
}

static int up3d_open(struct file *file)
{
	int ret= -1;
	
    struct up3d_video_ctx *ctx = video_drvdata(file);
	trace_in();

	ret = v4l2_fh_open(file);
	if (ret < 0)
	{
		UP3D_DEBUG("v4l2_fh_open failed ret:%d\n", ret);
		return ret;
	}

	_init_format(&ctx->cur_v4l2_format, ctx);
	trace_exit();
    return _vb_queue_init(&ctx->vb_queue, ctx);
}

static int up3d_release(struct file *file)
{
	int ret;

	struct up3d_video_ctx *ctx = video_drvdata(file);
    trace_in();

	vb2_queue_release(&ctx->vb_queue);

	ret = vb2_fop_release(file);
	UP3D_DEBUG("ret:%d", ret);

	ret = v4l2_fh_release(file);
	UP3D_DEBUG("ret:%d", ret);

	trace_exit();
    return ret;
}

static unsigned int up3d_poll(struct file *file, struct poll_table_struct *ptable)
{
	int ret = 0;

	ret = vb2_fop_poll(file, ptable);
	if(ret != 0)
		UP3D_DEBUG("ret:%d", ret);

    return ret;
}

static int up3d_mmap(struct file *file, struct vm_area_struct * vma)
{
	int ret = 0;

	trace_in();
	ret = vb2_fop_mmap(file, vma);
	UP3D_DEBUG("ret:%d", ret);
	trace_exit();
	
    return ret;
}

const struct v4l2_file_operations up3d_v4l2_fops = {
	.owner			= THIS_MODULE,
	.open           = up3d_open,
	.release        = up3d_release,
	// .read           = up3d_read,		// 本驱动只支持MMAP 所以这里不提供read/wirte方式的接口
	// .write          = up3d_write,
	.poll			= up3d_poll,
	.mmap           = up3d_mmap,
	// ioctl -> unlocked_ioctl -> video_ioctl2 -> my_v4l2_ioctl_ops(函数集) 相当于通过video_ioctl2中转了一次
	.unlocked_ioctl = video_ioctl2,		
};