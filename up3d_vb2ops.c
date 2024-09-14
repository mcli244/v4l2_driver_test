#include "up3d_vb2ops.h"
#include "up3d.h"
#include <linux/timer.h>
#include <media/videobuf-core.h>
#include <media/videobuf-vmalloc.h>

static struct timer_list up3d_timer;
static struct up3d_video_ctx *_g_ctx;

static void up3d_timer_function(struct timer_list *timer)
{
	int x,y;
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
		UP3D_DEBUG("vb2_plane_vaddr(&buf->vb.vb2_buf, 0):0x%x --- 0x%x", vb2_plane_vaddr(&up3d_vb->vb.vb2_buf, 0), up3d_vb->vb.vb2_buf.planes[0].mem_priv);
		// memset(up3d_vb->vb.vb2_buf.planes[0].mem_priv, 0xff, _g_ctx->cur_v4l2_format.fmt.pix.sizeimage);
		
		uint8_t *p = (uint8_t *)vb2_plane_vaddr(&up3d_vb->vb.vb2_buf, 0);

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

		// memset(, 0xff, _g_ctx->cur_v4l2_format.fmt.pix.sizeimage);
		up3d_vb->vb.vb2_buf.timestamp = ktime_get_ns();
		up3d_vb->vb.field = V4L2_FIELD_NONE;
		up3d_vb->vb.sequence = sequence++;
		vb2_set_plane_payload(&up3d_vb->vb.vb2_buf, 0, _g_ctx->cur_v4l2_format.fmt.pix.sizeimage);
		vb2_buffer_done(&up3d_vb->vb.vb2_buf, VB2_BUF_STATE_DONE);

		list_del_init(&up3d_vb->list);
	}
	// spin_unlock_irqrestore(&_g_ctx->vb_queue_lock, flags);

    /* 3. 修改timer的超时时间 : 30fps, 1秒里有30帧数据
     *    每1/30 秒产生一帧数据
     */
    mod_timer(timer, jiffies + HZ/30);
	trace_exit();
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
	// TODO:num_buffers\alloc_devs待研究

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
		buf->vaddr = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);
		buf->size = vb2_plane_size(&buf->vb.vb2_buf, 0);
		buf->prepared = true;

		vb2_set_plane_payload(&buf->vb.vb2_buf, 0, buf->size);

		dev_dbg(ctx->dev, "buffer[%d] addr=%pad size=%zu\n",
			vb->index, &buf->vaddr, buf->size);
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
	trace_in();
	
	// TODO:控制硬件开始采集 这里用定时器模拟数据产生

	// 测试定时器
	up3d_timer.expires = jiffies + 5;
	up3d_timer.function = up3d_timer_function;
	
	_g_ctx = vb2_get_drv_priv(q);
	UP3D_DEBUG("_g_ctx:%p 0x%x", _g_ctx, _g_ctx);

	add_timer(&up3d_timer);

	trace_exit();
	return 0;
}

/**  必要
 * 调用时机：
 * 作用：停止请求
 */
static void up3d_stop_streaming(struct vb2_queue *q)
{
	struct up3d_video_ctx *ctx = vb2_get_drv_priv(q);
	unsigned long flags;

	trace_in();

	#if 0
	// TODO:控制硬件停止采集
	spin_lock_irqsave(&ctx->vb_queue_lock, flags);
	while (!list_empty(&ctx->vb_queue_active)) {
		struct up3d_vb2_buf *buf = list_entry(ctx->vb_queue_active.next, struct up3d_vb2_buf, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&ctx->vb_queue_lock, flags);

	#endif
	del_timer(&up3d_timer);
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
	trace_in();

	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct up3d_vb2_buf *buf = container_of(vbuf, struct up3d_vb2_buf, vb);

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
