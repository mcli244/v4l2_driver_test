#include "up3d_vb2ops.h"
#include "up3d.h"
#include <linux/timer.h>
#include <media/videobuf-core.h>
#include <media/videobuf-vmalloc.h>

// static struct up3d_video_ctx *_g_ctx;

#define UP3D_STA_STOP 0
#define UP3D_STA_RUN 1
#define UP3D_STA_PAUSE 2
static int up3d_timer_stop = UP3D_STA_STOP;

#if 0
static void _up3d_vb2_fill(struct up3d_video_ctx *_g_ctx)
{
	int x,y;
	uint8_t *p;
    struct up3d_vb2_buf *up3d_vb;
	// int flags;
	static uint32_t sequence = 0;
	
    
	trace_in();
    /* 1. 构造数据: 从队列头部取出第1个videobuf, 填充数据
     */

	// spin_lock_irqsave(&_g_ctx->vb_queue_lock, flags);
	if(!list_empty(&_g_ctx->vb_queue_active)) 
	{
		up3d_vb = list_entry(_g_ctx->vb_queue_active.next, struct up3d_vb2_buf, list);

		// 填充数据
		// UP3D_DEBUG("vb2_plane_vaddr(&buf->vb.vb2_buf, 0):0x%x --- 0x%x", vb2_plane_vaddr(&up3d_vb->vb.vb2_buf, 0), up3d_vb->vb.vb2_buf.planes[0].mem_priv);
		// memset(up3d_vb->vb.vb2_buf.planes[0].mem_priv, 0xff, _g_ctx->cur_v4l2_format.fmt.pix.sizeimage);
		
		p = (uint8_t *)vb2_plane_vaddr(&up3d_vb->vb.vb2_buf, 0);

		if(_g_ctx->cur_v4l2_format.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV)
		{
			for(x=0; x<_g_ctx->cur_v4l2_format.fmt.pix.width; x++)
			{
				for(y=0; y<_g_ctx->cur_v4l2_format.fmt.pix.height; y++)
				{	
					// YUYV
					*(p+0) = 0x00;
					*(p+1) = (sequence*10) % 0xff;
					*(p+2) = 0x00;
					*(p+3) = 0xff;
					p += 4;
				}
			}
		}
		else if(_g_ctx->cur_v4l2_format.fmt.pix.pixelformat == V4L2_PIX_FMT_RGB24)
		{
			for(x=0; x<_g_ctx->cur_v4l2_format.fmt.pix.width; x++)
			{
				for(y=0; y<_g_ctx->cur_v4l2_format.fmt.pix.height; y++)
				{	
					// RGB
					*(p+0) = 0x00;
					*(p+1) = (sequence*10) % 0xff;
					*(p+2) = 0x00;
					p += 3;
				}
			}
		}
		else if(_g_ctx->cur_v4l2_format.fmt.pix.pixelformat == V4L2_PIX_FMT_GREY)
		{
			if(_g_ctx->ddr_addr)
			{	
				memcpy(p, _g_ctx->img_addrs[_g_ctx->img_index], _g_ctx->cur_v4l2_format.fmt.pix.sizeimage);
			}
			else
			{
				UP3D_DEBUG("ddr_addr is NULL\n");
			}
		}
		else
		{
			// 其他格式
			// memset(, 0xff, _g_ctx->cur_v4l2_format.fmt.pix.sizeimage);
		}
		

		// memset(, 0xff, _g_ctx->cur_v4l2_format.fmt.pix.sizeimage);
		up3d_vb->vb.vb2_buf.timestamp = ktime_get_ns();
		up3d_vb->vb.field = V4L2_FIELD_NONE;
		up3d_vb->vb.sequence = sequence++;
		vb2_set_plane_payload(&up3d_vb->vb.vb2_buf, 0, _g_ctx->cur_v4l2_format.fmt.pix.sizeimage);
		vb2_buffer_done(&up3d_vb->vb.vb2_buf, VB2_BUF_STATE_DONE);

		list_del_init(&up3d_vb->list);
	}
	// spin_unlock_irqrestore(&_g_ctx->vb_queue_lock, flags);

}
#endif

static void _up3d_vb2_fill_patch(struct up3d_video_ctx *_g_ctx)
{
	int i;
	uint8_t *p;
    struct up3d_vb2_buf *up3d_vb;
	static uint32_t sequence = 0;
	trace_in();
   
	// 特殊处理，针对FPGA给到的图像，一个中断读取两张图像

	// spin_lock_irqsave(&_g_ctx->vb_queue_lock, flags);
	for(i=0; i<2; i++)
	{
		if(!list_empty(&_g_ctx->vb_queue_active)) 
		{
			up3d_vb = list_entry(_g_ctx->vb_queue_active.next, struct up3d_vb2_buf, list);

			// 填充数据
			// UP3D_DEBUG("vb2_plane_vaddr(&buf->vb.vb2_buf, 0):0x%x --- 0x%x", vb2_plane_vaddr(&up3d_vb->vb.vb2_buf, 0), up3d_vb->vb.vb2_buf.planes[0].mem_priv);
			// memset(up3d_vb->vb.vb2_buf.planes[0].mem_priv, 0xff, _g_ctx->cur_v4l2_format.fmt.pix.sizeimage);
			
			p = (uint8_t *)vb2_plane_vaddr(&up3d_vb->vb.vb2_buf, 0);
			if(_g_ctx->cur_v4l2_format.fmt.pix.pixelformat == V4L2_PIX_FMT_GREY)
			{
				if(_g_ctx->ddr_addr)
				{	
					memcpy(p, _g_ctx->img_addrs[_g_ctx->img_index] + _g_ctx->cur_v4l2_format.fmt.pix.sizeimage * i, _g_ctx->cur_v4l2_format.fmt.pix.sizeimage);
				}
				else
				{
					UP3D_DEBUG("ddr_addr is NULL\n");
				}
			}
			else
			{
				// 其他格式
				// memset(, 0xff, _g_ctx->cur_v4l2_format.fmt.pix.sizeimage);
			}
			

			// memset(, 0xff, _g_ctx->cur_v4l2_format.fmt.pix.sizeimage);
			up3d_vb->vb.vb2_buf.timestamp = ktime_get_ns();
			up3d_vb->vb.field = V4L2_FIELD_NONE;
			up3d_vb->vb.sequence = sequence++;
			vb2_set_plane_payload(&up3d_vb->vb.vb2_buf, 0, _g_ctx->cur_v4l2_format.fmt.pix.sizeimage);
			vb2_buffer_done(&up3d_vb->vb.vb2_buf, VB2_BUF_STATE_DONE);

			list_del_init(&up3d_vb->list);
		}
	}
}

static irqreturn_t pl_cap_intc_irq_handler(int irq, void *dev_id)
{
	struct up3d_video_ctx *ctx = (struct up3d_video_ctx *)dev_id;

	if(ctx == NULL)
	{
		printk(KERN_ERR "ctx is NULL\n");
		return IRQ_HANDLED;
	}

	_up3d_vb2_fill_patch(ctx);
	// _up3d_vb2_fill(ctx);

	ctx->img_index++;
	if(ctx->img_index >= ctx->img_blk_count)
	{
		ctx->img_index = 0;
	}

	return IRQ_HANDLED;
}

/** 
 * 调用时机：由ioctl命令VIDIOC_REQBUFS和VIDIOC_CREATE_BUFS调用时被调用
 * 作用：设置参数
 */
static int up3d_queue_setup(struct vb2_queue *q,
			   unsigned int *num_buffers, unsigned int *num_planes,
			   unsigned int sizes[], struct device *alloc_devs[])
{
	struct up3d_video_ctx *ctx = vb2_get_drv_priv(q);

	trace_in();

	*num_planes = 1;	// 目前只支持单层，设为1
	sizes[0] = ctx->cur_v4l2_format.fmt.pix.sizeimage;

	trace_exit();

	return 0;
};

/** 
 * 调用时机：缓冲区被放入到队列前调用此函数
 * 作用：动需要执行一些初始化工作或获取、修改缓冲区，若驱动支持VIDIOC_CREATE_BUFS，
 * 		 则需要验证缓冲区的大小，若有错误发生，则缓冲区不会入队。
 */
static int up3d_buf_prepare(struct vb2_buffer *vb)
{
	struct up3d_video_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct up3d_vb2_buf *buf = container_of(vbuf, struct up3d_vb2_buf, vb);
	unsigned long size;

	int ret = 0;

	trace_in();

	size = ctx->cur_v4l2_format.fmt.pix.sizeimage;

	if (vb2_plane_size(vb, 0) < size) {
		dev_err(ctx->dev, "%s data will not fit into plane (%lu < %lu)\n",
			__func__, vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	if (!buf->prepared) {
		/* Get memory addresses */
		buf->prepared = true;
		vb2_set_plane_payload(&buf->vb.vb2_buf, 0, vb2_plane_size(&buf->vb.vb2_buf, 0));
	}

	// 检查缓冲区虚拟地址是否存在和payload是否正确设置
	if (vb2_plane_vaddr(vb, 0) &&
		vb2_get_plane_payload(vb, 0) > vb2_plane_size(vb, 0)) {
		ret = -EINVAL;
		goto out;
	}
	trace_exit();
	return 0;
out:
	trace_exit();
	return ret;
}

static void up3d_buf_finish(struct vb2_buffer *vb)
{
	trace_in();
	trace_exit();
}

/**  必要
 * 调用时机：
 * 作用：缓冲区加入队列
 */
static void up3d_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct up3d_vb2_buf *buf = container_of(vbuf, struct up3d_vb2_buf, vb);
	struct up3d_video_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	trace_in();

	spin_lock(&ctx->vb_queue_lock);
	list_add_tail(&buf->list, &ctx->vb_queue_active);
	spin_unlock(&ctx->vb_queue_lock);

	trace_exit();
}

static int up3d_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct up3d_video_ctx *ctx = vb2_get_drv_priv(q);

	trace_in();

	if(up3d_timer_stop == UP3D_STA_PAUSE)
	{
		enable_irq(ctx->irq);
		up3d_timer_stop = UP3D_STA_RUN;
	}
	else if(up3d_timer_stop == UP3D_STA_STOP)
	{
		/* 申请中断 */
		if (devm_request_irq(ctx->dev, ctx->irq, pl_cap_intc_irq_handler, IRQF_TRIGGER_RISING, "pl_cap_intc", ctx)) {
			dev_err(ctx->dev, "Failed to request IRQ\n");
			return -EINVAL;
		}
		up3d_timer_stop = UP3D_STA_RUN;
	}
	else{
		// do nothing
	}
	
	trace_exit();
	return 0;
}

/**  必要
 * 调用时机：
 * 作用：停止请求
 */
static void up3d_stop_streaming(struct vb2_queue *q)
{
	struct up3d_vb2_buf *up3d_vb, *tmp;
	struct up3d_video_ctx *ctx = vb2_get_drv_priv(q);

	trace_in();

	if(up3d_timer_stop == UP3D_STA_RUN)
	{
		disable_irq_nosync(ctx->irq);
		up3d_timer_stop = UP3D_STA_PAUSE;	// TODO: 这里没有完全释放IRQ，只是暂停了中断，释放中断放到remove中

		// 关闭流时，释放所有仍然处于 ACTIVE 状态的 buffer
		list_for_each_entry_safe(up3d_vb, tmp, &ctx->vb_queue_active, list) {
				list_del(&up3d_vb->list);
				vb2_buffer_done(&up3d_vb->vb.vb2_buf, VB2_BUF_STATE_ERROR);
			}
	}
	
	trace_exit();
}

static void up3d_wait_prepare(struct vb2_queue *q)
{
	trace_in();
	trace_exit();
}

static void up3d_wait_finish(struct vb2_queue *q)
{
	trace_in();
	trace_exit();
}

static int up3d_buf_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct up3d_vb2_buf *buf = container_of(vbuf, struct up3d_vb2_buf, vb);

	trace_in();

	INIT_LIST_HEAD(&buf->list);
	UP3D_DEBUG("vb->vb2_queue:%p", vb->vb2_queue);
	UP3D_DEBUG("vb->index:%d type:0x%x memory:0x%x num_planes:%d timestamp:%lld state:%d", 
		vb->index, vb->type, vb->memory, vb->num_planes, vb->timestamp, vb->state);

	UP3D_DEBUG(" vb->planes ###################### ");
	UP3D_DEBUG("mem_priv:%p dbuf_mapped:%d bytesused:%d length:%d min_length:%d offset:0x%x data_offset:0x%x", 
		vb->planes[0].mem_priv, vb->planes[0].dbuf_mapped, vb->planes[0].bytesused, vb->planes[0].length, 
		vb->planes[0].min_length, vb->planes[0].m.offset, vb->planes[0].data_offset);



	trace_exit();
	return 0;
}

static void up3d_buf_cleanup(struct vb2_buffer *vb)
{
	trace_in();
	trace_exit();
}


const struct vb2_ops up3d_vb2_ops = {
	.queue_setup		= up3d_queue_setup,			// 当用户空间调用VIDIOC_REQBUFS时，此回调用于初始化队列，分配缓冲区。
	.buf_init			= up3d_buf_init,			// 对每个新分配的缓冲区进行初始化。
	.buf_prepare		= up3d_buf_prepare,			// 准备一个缓冲区以供采集数据使用。
	.buf_queue			= up3d_buf_queue,			// 将准备好的缓冲区入队，使其可以被驱动程序使用。
	.start_streaming	= up3d_start_streaming,		// 开始缓冲区的数据采集流程。
	.buf_finish			= up3d_buf_finish,			// 在缓冲区完成数据采集后，进行必要的后处理。
	.stop_streaming		= up3d_stop_streaming,		// 停止数据采集，并进行清理。
	.wait_prepare		= up3d_wait_prepare,
	.wait_finish		= up3d_wait_finish,
	.buf_cleanup		= up3d_buf_cleanup,
};
