#include "up3d_vb2ops.h"
#include "up3d.h"

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

static void up3d_buf_finish(struct vb2_buffer *vb)
{
	trace_in();
}

/**  必要
 * 调用时机：
 * 作用：缓冲区加入队列
 */
static void up3d_buf_queue(struct vb2_buffer *vb)
{
	
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct up3d_vb2_buf *my_vb = container_of(vbuf, struct up3d_vb2_buf, vb);
	struct up3d_video_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	trace_in();

	spin_lock(&ctx->vb_queue_lock);
	list_add_tail(&my_vb->list, &ctx->vb_queue_active);
	spin_unlock(&ctx->vb_queue_lock);

	trace_exit();
}

static int up3d_start_streaming(struct vb2_queue *q, unsigned int count)
{
	trace_in();

	// TODO:控制硬件开始采集


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

	// TODO:控制硬件停止采集
	spin_lock_irqsave(&ctx->vb_queue_lock, flags);
	while (!list_empty(&ctx->vb_queue_active)) {
		struct up3d_vb2_buf *buf = list_entry(ctx->vb_queue_active.next, struct up3d_vb2_buf, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&ctx->vb_queue_lock, flags);

}

static void up3d_wait_prepare(struct vb2_queue *q)
{
	trace_in();
}

static void up3d_wait_finish(struct vb2_queue *q)
{
	trace_in();
}

static int up3d_buf_init(struct vb2_buffer *vb)
{
	trace_in();
	return 0;
}

static void up3d_buf_cleanup(struct vb2_buffer *vb)
{
	trace_in();
}


const struct vb2_ops up3d_vb2_ops = {
	.queue_setup		= up3d_queue_setup,
	.buf_prepare		= up3d_buf_prepare,
	.buf_finish			= up3d_buf_finish,
	.buf_queue			= up3d_buf_queue,
	.start_streaming	= up3d_start_streaming,
	.stop_streaming		= up3d_stop_streaming,
	.wait_prepare		= up3d_wait_prepare,
	.wait_finish		= up3d_wait_finish,
	.buf_init			= up3d_buf_init,
	.buf_cleanup		= up3d_buf_cleanup,
};
