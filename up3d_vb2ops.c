#include "up3d_vb2ops.h"
#include "up3d.h"
#include <linux/timer.h>
#include <linux/delay.h>
#include "up3d_fpga.h"

#define MY_SOFTIRQ_VEC 26

// 定时器触发数据填充
// #define TIMER_TRIGGER_FILL

void up3d_vb2_tasklet_handler(unsigned long data);

static void _up3d_vb2_fill_patch(struct up3d_video_ctx *_g_ctx)
{
	int i;
	uint8_t *p;
	struct up3d_vb2_buf *up3d_vb;
	static uint32_t sequence = 0;
	unsigned long flags;
	// 特殊处理，针对FPGA给到的图像，一个中断读取两张图像

	spin_lock_irqsave(&_g_ctx->vb_queue_lock, flags);

	for (i = 0; i < 1; i++)
	{
		if (!list_empty(&_g_ctx->vb_queue_active))
		{
			up3d_vb = list_entry(_g_ctx->vb_queue_active.next, struct up3d_vb2_buf, list);
			if(up3d_vb == NULL)
			{
				dev_err(_g_ctx->dev, "up3d_vb is NULL\n");
				break;
			}
			p = (uint8_t *)vb2_plane_vaddr(&up3d_vb->vb.vb2_buf, 0);
			if(p)
			{	
				if (_g_ctx->cur_v4l2_format.fmt.pix.pixelformat == V4L2_PIX_FMT_GREY)
				{
					if (_g_ctx->ddr_addr)
					{
						// memcpy(p, _g_ctx->img_addrs[_g_ctx->img_index], _g_ctx->cur_v4l2_format.fmt.pix.sizeimage);
						// 填充大块条纹数据，根据宽高（更大块的：每8行一组切换，减少交替次数）
						{
							// uint32_t width = _g_ctx->cur_v4l2_format.fmt.pix.width;
							// uint32_t height = _g_ctx->cur_v4l2_format.fmt.pix.height;
							uint8_t *dst = p;
							// uint32_t row, blk_size = 32;
							// uint8_t value;

							// for (row = 0; row < height; row++) {
							// 	// 每8行为一块交替
							// 	value = ((row / blk_size) % 2 == 0) ? 0xFF : 0x00;
							// 	memset(dst + row * width, value, width);
							// }
							memcpy(dst, _g_ctx->ddr_addr + 2*1024*1024, _g_ctx->cur_v4l2_format.fmt.pix.sizeimage);
						}
					}
					else
					{
						dev_err(_g_ctx->dev, "ddr_addr is NULL\n");
					}
				}
				else
				{
					// 其他格式
					dev_err(_g_ctx->dev, "not support this format\n");
				}
			}
			else
			{
				dev_err(_g_ctx->dev, "vb2_plane_vaddr is NULL\n");
			}

			up3d_vb->vb.vb2_buf.timestamp = ktime_get_ns();
			up3d_vb->vb.field = V4L2_FIELD_NONE;
			up3d_vb->vb.sequence = sequence++;
			vb2_set_plane_payload(&up3d_vb->vb.vb2_buf, 0, _g_ctx->cur_v4l2_format.fmt.pix.sizeimage);
			vb2_buffer_done(&up3d_vb->vb.vb2_buf, VB2_BUF_STATE_DONE);

			list_del_init(&up3d_vb->list);

			_g_ctx->device_info.vb_free--;
			if(_g_ctx->device_info.vb_free < _g_ctx->device_info.vb_free_min)
			{
				_g_ctx->device_info.vb_free_min = _g_ctx->device_info.vb_free;
			}

		} else 
		{
			_g_ctx->device_info.vb_queue_overflow++;
		}
	}

	spin_unlock_irqrestore(&_g_ctx->vb_queue_lock, flags);
}

void up3d_vb2_tasklet_handler(unsigned long data)
{
    struct up3d_video_ctx *ctx = (void *)data;
    struct up3d_vb2_buf *vb;
    struct up3d_vb2_buf *next;
    unsigned long flags;

    if (!ctx)
        return;

    spin_lock_irqsave(&ctx->vb_queue_lock, flags);

    /* 1. 完成当前 buffer */
	if(ctx->current_vb != NULL) {
		dev_err(ctx->dev, "current_vb is not NULL\n");
		return;
	}
    vb = ctx->current_vb;
    ctx->current_vb = NULL;

    if (vb) {
        spin_unlock_irqrestore(&ctx->vb_queue_lock, flags);
        vb->vb.vb2_buf.timestamp = ktime_get_ns();
        vb->vb.field = V4L2_FIELD_NONE;
        vb2_buffer_done(&vb->vb.vb2_buf, VB2_BUF_STATE_DONE);
    } else {
        spin_unlock_irqrestore(&ctx->vb_queue_lock, flags);
    }

    /* 2. 取下一个 buffer */
    spin_lock_irqsave(&ctx->vb_queue_lock, flags);

    if (list_empty(&ctx->vb_queue_active)) {
        spin_unlock_irqrestore(&ctx->vb_queue_lock, flags);
        return;
    }

    next = list_first_entry(&ctx->vb_queue_active,
                            struct up3d_vb2_buf,
                            list);

    list_del_init(&next->list);
    ctx->current_vb = next;

    spin_unlock_irqrestore(&ctx->vb_queue_lock, flags);

    /* 3. 启动 FPGA */
	up3d_fpga_ctrl(ctx, 0);
    up3d_fpga_set_output_image_addr(ctx, vb2_dma_contig_plane_dma_addr(&next->vb.vb2_buf, 0));
    up3d_fpga_ctrl(ctx, 1);

}

static irqreturn_t pl_cap_intc_irq_handler(int irq, void *dev_id)
{
	struct up3d_video_ctx *ctx = (struct up3d_video_ctx *)dev_id;

	tasklet_schedule(&ctx->vb2_tasklet);
	atomic_inc(&ctx->device_info.irq_count);

	return IRQ_HANDLED;
}

static int up3d_queue_setup(struct vb2_queue *q,
							unsigned int *num_buffers, unsigned int *num_planes,
							unsigned int sizes[], struct device *alloc_devs[])
{
	struct up3d_video_ctx *ctx = vb2_get_drv_priv(q);

	*num_planes = 1; // 目前只支持单层，设为1
	sizes[0] = ctx->cur_v4l2_format.fmt.pix.sizeimage;

	return 0;
};

static int up3d_buf_prepare(struct vb2_buffer *vb)
{
	struct up3d_video_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct up3d_vb2_buf *buf = container_of(vbuf, struct up3d_vb2_buf, vb);
	unsigned long size;
	int ret = 0;

	size = ctx->cur_v4l2_format.fmt.pix.sizeimage;
	if (vb2_plane_size(vb, 0) < size)
	{
		dev_err(ctx->dev, "%s data will not fit into plane (%lu < %lu)\n",
				__func__, vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	if (!buf->prepared)
	{
		/* Get memory addresses */
		buf->prepared = true;
		vb2_set_plane_payload(&buf->vb.vb2_buf, 0, vb2_plane_size(&buf->vb.vb2_buf, 0));
	}

	if (vb2_plane_vaddr(vb, 0) &&
		vb2_get_plane_payload(vb, 0) > vb2_plane_size(vb, 0))
	{
		ret = -EINVAL;
		goto out;
	}
	return 0;

out:
	return ret;
}

static void up3d_buf_finish(struct vb2_buffer *vb)
{
}

static void up3d_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct up3d_vb2_buf *buf = container_of(vbuf, struct up3d_vb2_buf, vb);
	struct up3d_video_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);


	dma_addr_t dma_addr = vb2_dma_contig_plane_dma_addr(vb, 0);
	dev_info(ctx->dev, "up3d_buf_queue: Buffer %pad\n", &dma_addr);

	spin_lock(&ctx->vb_queue_lock);
	list_add_tail(&buf->list, &ctx->vb_queue_active);
	spin_unlock(&ctx->vb_queue_lock);
	ctx->device_info.vb_free++;
}

#ifdef TIMER_TRIGGER_FILL
static void up3d_timer_callback(struct timer_list *t);
#endif

static int up3d_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct up3d_vb2_buf *up3d_vb, *tmp;
	struct up3d_video_ctx *ctx = vb2_get_drv_priv(q);

	dev_info(ctx->dev, "up3d_start_streaming: ctx %p\n", ctx);

#ifdef TIMER_TRIGGER_FILL
	// 启动定时器方式填充，33ms周期
	if (!timer_pending(&ctx->stream_timer)) {
		timer_setup(&ctx->stream_timer, up3d_timer_callback, 0);
		ctx->stream_timer.expires = jiffies + msecs_to_jiffies(33);
		add_timer(&ctx->stream_timer);
	}
#else
	if (atomic_read(&ctx->device_info.status) == UP3D_STA_PAUSE)
	{
		enable_irq(ctx->irq); // TODO: 后续应该是通过AXI-IIC通知FPGA开始产生中断
		atomic_set(&ctx->device_info.irq_is_disable, 0);
		atomic_set(&ctx->device_info.status, UP3D_STA_RUN);
	}
	else if (atomic_read(&ctx->device_info.status) == UP3D_STA_STOP)
	{
		/* 申请中断 */
		dev_info(ctx->dev, "up3d_start_streaming: request IRQ\n");
		if (devm_request_irq(ctx->dev, ctx->irq, pl_cap_intc_irq_handler, IRQF_TRIGGER_RISING, "pl_cap_intc", ctx))
		{
			dev_err(ctx->dev, "Failed to request IRQ\n");
			return -EINVAL;
		}
		atomic_set(&ctx->device_info.status, UP3D_STA_RUN);

		dev_info(ctx->dev, "up3d_start_streaming: set status to RUN\n");
		atomic_set(&ctx->device_info.irq_is_disable, 0);
		ctx->device_info.vb_total = 0;
		list_for_each_entry_safe(up3d_vb, tmp, &ctx->vb_queue_active, list)
		{
			ctx->device_info.vb_total++;
		}
		ctx->device_info.vb_free = ctx->device_info.vb_total;
		ctx->device_info.vb_free_min = ctx->device_info.vb_total;

		dev_info(ctx->dev, "up3d_start_streaming: get one buffer from queue\n");
		// 从队列里面取一个缓存，写给FPGA
		{
			unsigned long flags;
			struct up3d_vb2_buf *vb;
			uint32_t dma_addr;
			
			spin_lock_irqsave(&ctx->vb_queue_lock, flags);
			if (list_empty(&ctx->vb_queue_active)) {
				spin_unlock_irqrestore(&ctx->vb_queue_lock, flags);
				return -EINVAL;
			}
			vb = list_first_entry(&ctx->vb_queue_active,
								  struct up3d_vb2_buf,
								  list);
			list_del_init(&vb->list);
			ctx->current_vb = vb;
			spin_unlock_irqrestore(&ctx->vb_queue_lock, flags);
			
			dma_addr = vb2_dma_contig_plane_dma_addr(&vb->vb.vb2_buf, 0);
			if (!dma_addr) {
				dev_err(ctx->dev, "invalid dma\n");
				return -EINVAL;
			}
			
			up3d_fpga_ctrl(ctx, 0);
			up3d_fpga_set_output_image_addr(ctx, dma_addr);
			up3d_fpga_ctrl(ctx, 1);
			dev_info(ctx->dev, "up3d_start_streaming: set output image address to 0x%x\n", dma_addr);
		}
	}
	// timer分支下这里其实啥也不做，只是设置status/统计信息
#endif

#if defined(TIMER_TRIGGER_FILL)
	atomic_set(&ctx->device_info.status, UP3D_STA_RUN);
	atomic_set(&ctx->device_info.irq_is_disable, 0);
	ctx->device_info.vb_total = 0;
	list_for_each_entry_safe(up3d_vb, tmp, &ctx->vb_queue_active, list)
	{
		ctx->device_info.vb_total++;
	}
	ctx->device_info.vb_free = ctx->device_info.vb_total;
	ctx->device_info.vb_free_min = ctx->device_info.vb_total;
#else
	atomic_set(&ctx->device_info.status, UP3D_STA_RUN);
#endif

	return 0;
}

#ifdef TIMER_TRIGGER_FILL
// 定时器回调，33ms一次填充数据
static void up3d_timer_callback(struct timer_list *t)
{
	struct up3d_video_ctx *ctx = from_timer(ctx, t, stream_timer);

	// 主动填充数据（原本由中断驱动 tasklet 触发）
	_up3d_vb2_fill_patch(ctx);

	// 重新启动定时触发
	ctx->stream_timer.expires = jiffies + msecs_to_jiffies(33);
	add_timer(&ctx->stream_timer);
}
#endif

static void up3d_stop_streaming(struct vb2_queue *q)
{
	struct up3d_vb2_buf *up3d_vb, *tmp;
	struct up3d_video_ctx *ctx = vb2_get_drv_priv(q);

#if defined(TIMER_TRIGGER_FILL)
	if (up3d_status == UP3D_STA_RUN)
	{
		// 停止定时器分支
		del_timer_sync(&ctx->stream_timer);

		up3d_status = UP3D_STA_PAUSE;
		ctx->device_info.status = up3d_status;

		list_for_each_entry_safe(up3d_vb, tmp, &ctx->vb_queue_active, list)
		{
			list_del(&up3d_vb->list);
			vb2_buffer_done(&up3d_vb->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		}
		atomic_set(&ctx->device_info.irq_is_disable, 1);
	}
#else
	if (atomic_read(&ctx->device_info.status) == UP3D_STA_RUN)
	{
		gpiod_set_value_cansleep(ctx->completed_gpio, 0);
		msleep(10);
		// disable_irq_nosync(ctx->irq);	  // TODO: 后续应该是通过AXI-IIC通知FPGA停止产生中断
		disable_irq(ctx->irq);	  // TODO: 后续应该是通过AXI-IIC通知FPGA停止产生中断
		atomic_set(&ctx->device_info.status, UP3D_STA_PAUSE); // note: 这里没有完全释放IRQ，只是暂停了中断，释放中断放到remove中
		
		list_for_each_entry_safe(up3d_vb, tmp, &ctx->vb_queue_active, list)
		{
			list_del(&up3d_vb->list);
			vb2_buffer_done(&up3d_vb->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		}
		atomic_set(&ctx->device_info.irq_is_disable, 1);
	}
#endif
}

static void up3d_wait_prepare(struct vb2_queue *q)
{
}

static void up3d_wait_finish(struct vb2_queue *q)
{
}

static int up3d_buf_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct up3d_vb2_buf *buf = container_of(vbuf, struct up3d_vb2_buf, vb);
	struct up3d_video_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	dev_info(ctx->dev, "up3d_buf_init: Buffer %p, ctx %p\n", buf, ctx);
	INIT_LIST_HEAD(&buf->list);
	return 0;
}

static void up3d_buf_cleanup(struct vb2_buffer *vb)
{
}

const struct vb2_ops up3d_vb2_ops = {
	.queue_setup 		= up3d_queue_setup,
	.buf_init 			= up3d_buf_init,
	.buf_prepare 		= up3d_buf_prepare,
	.buf_queue 			= up3d_buf_queue,
	.start_streaming 	= up3d_start_streaming,
	.buf_finish 		= up3d_buf_finish,
	.stop_streaming 	= up3d_stop_streaming,
	.wait_prepare 		= up3d_wait_prepare,
	.wait_finish 		= up3d_wait_finish,
	.buf_cleanup 		= up3d_buf_cleanup,
};
