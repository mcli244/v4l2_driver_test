#include "up3d_vb2ops.h"
#include "up3d.h"
#include <linux/timer.h>
#include <linux/delay.h>
#include "up3d_fpga.h"
#include <linux/delay.h>
#include <linux/ktime.h>

static void up3d_frame_process(struct up3d_video_ctx *ctx)
{
    struct up3d_vb2_buf *vb;
    struct up3d_vb2_buf *next;
    unsigned long flags;
	static u64 prev_irq_time_ns = 0;
	u64 curr_irq_time_ns;
    u32 interval_ms = 0;

    if (!ctx){
		return;
	}

    {
        int empty_count = 0;
        int max_count = 50; // 10ms * 50 = 500ms = 0.5s
        bool got_buffer = false;

        while (1) {
            spin_lock_irqsave(&ctx->vb_queue_lock, flags);
            if (!list_empty(&ctx->vb_queue_active)) {
                got_buffer = true;
				spin_unlock_irqrestore(&ctx->vb_queue_lock, flags);
                break;
            }
            spin_unlock_irqrestore(&ctx->vb_queue_lock, flags);
            msleep(10);
            empty_count++;
            if (empty_count >= max_count) {
				if(ctx->current_vb != NULL){
					up3d_fpga_ctrl(ctx, 0);
					up3d_fpga_set_output_image_addr(ctx, vb2_dma_contig_plane_dma_addr(&ctx->current_vb->vb.vb2_buf, 0));
					up3d_fpga_ctrl(ctx, 1);
					ctx->device_info.fpga_discarded_frames_cnt++;
					return;
				}else {
					dev_err(ctx->dev, "vb_queue_active still empty after %d ms, exit frame process\n", max_count * 10);
					return;
				}   
            }
        }
    }

    spin_lock_irqsave(&ctx->vb_queue_lock, flags);
    vb = ctx->current_vb;
    ctx->current_vb = NULL;

    if (vb) {
        spin_unlock_irqrestore(&ctx->vb_queue_lock, flags);
        vb->vb.vb2_buf.timestamp = ktime_get_ns();
        vb->vb.field = V4L2_FIELD_NONE;
        vb2_buffer_done(&vb->vb.vb2_buf, VB2_BUF_STATE_DONE);
    } else {
        spin_unlock_irqrestore(&ctx->vb_queue_lock, flags);
		dev_err(ctx->dev, "vb is NULL\n");
    }

	spin_lock_irqsave(&ctx->vb_queue_lock, flags);
    next = list_first_entry(&ctx->vb_queue_active,
                            struct up3d_vb2_buf,
                            list);

    list_del_init(&next->list);
    ctx->current_vb = next;

    spin_unlock_irqrestore(&ctx->vb_queue_lock, flags);
	
	
	curr_irq_time_ns = ktime_get_ns();
    if (prev_irq_time_ns != 0) {
        interval_ms = (u32)((curr_irq_time_ns - prev_irq_time_ns) / 1000000);
        ctx->device_info.fpga_enable_interval_time_ms = interval_ms;
    }
    prev_irq_time_ns = curr_irq_time_ns;

    /* 3. 启动 FPGA */
	up3d_fpga_ctrl(ctx, 0);
    up3d_fpga_set_output_image_addr(ctx, vb2_dma_contig_plane_dma_addr(&next->vb.vb2_buf, 0));
    up3d_fpga_ctrl(ctx, 1);
}

void up3d_irq_work_handler(struct work_struct *work)
{
	static int count = 0;
	struct up3d_video_ctx *ctx = container_of(work, struct up3d_video_ctx, irq_work);
	// dev_dbg(ctx->dev, "up3d_irq_work_handler: irq work callback\n");
	if(count < 10){
		count++;
		msleep(33);
	}
	up3d_frame_process(ctx);
}

irqreturn_t pl_cap_intc_irq_handler(int irq, void *dev_id)
{
    static u64 prev_irq_time_ns = 0;
    struct up3d_video_ctx *ctx = (struct up3d_video_ctx *)dev_id;
    u64 curr_irq_time_ns;
    u32 interval_ms = 0;

    // dev_dbg(ctx->dev, "pl_cap_intc_irq_handler: irq handler %d\n", atomic_read(&ctx->device_info.irq_count));

    curr_irq_time_ns = ktime_get_ns();
    if (prev_irq_time_ns != 0) {
        interval_ms = (u32)((curr_irq_time_ns - prev_irq_time_ns) / 1000000);
        ctx->device_info.irq_interval_time_ms = interval_ms;
    }
    prev_irq_time_ns = curr_irq_time_ns;

    up3d_fpga_irq_clear(ctx);
    schedule_work(&ctx->irq_work);
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
	static ktime_t last_time;
	ktime_t now = ktime_get();
	s64 interval_ms = 0;

	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct up3d_vb2_buf *buf = container_of(vbuf, struct up3d_vb2_buf, vb);
	struct up3d_video_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	dma_addr_t dma_addr = vb2_dma_contig_plane_dma_addr(vb, 0);

	// Measure interval between two entries
	if (last_time != 0)
		interval_ms = ktime_to_ms(ktime_sub(now, last_time));
	last_time = now;
	ctx->device_info.buf_queue_interval_time_ms = (uint32_t)interval_ms;

	// dev_dbg(ctx->dev, "up3d_buf_queue: Buffer %pad, interval since last queue: %lld ms\n", &dma_addr, interval_ms);

	spin_lock(&ctx->vb_queue_lock);
	list_add_tail(&buf->list, &ctx->vb_queue_active);
	spin_unlock(&ctx->vb_queue_lock);
	ctx->device_info.vb_free++;
}

static int up3d_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct up3d_vb2_buf *up3d_vb, *tmp;
	struct up3d_video_ctx *ctx = vb2_get_drv_priv(q);

	dev_dbg(ctx->dev, "up3d_start_streaming: ctx %p\n", ctx);

	if (atomic_read(&ctx->device_info.status) == UP3D_STA_STOP)
	{
		atomic_set(&ctx->device_info.status, UP3D_STA_RUN);

		dev_dbg(ctx->dev, "up3d_start_streaming: set status to RUN\n");
		atomic_set(&ctx->device_info.irq_is_disable, 0);
		ctx->device_info.vb_total = 0;
		list_for_each_entry_safe(up3d_vb, tmp, &ctx->vb_queue_active, list)
		{
			ctx->device_info.vb_total++;
		}
		ctx->device_info.vb_free = ctx->device_info.vb_total;
		ctx->device_info.vb_free_min = ctx->device_info.vb_total;

		dev_dbg(ctx->dev, "up3d_start_streaming: get one buffer from queue\n");
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
			dev_dbg(ctx->dev, "up3d_start_streaming: get one buffer from queue ctx->current_vb %p\n", ctx->current_vb);
			spin_unlock_irqrestore(&ctx->vb_queue_lock, flags);
			
			dma_addr = vb2_dma_contig_plane_dma_addr(&vb->vb.vb2_buf, 0);
			if (!dma_addr) {
				dev_err(ctx->dev, "invalid dma\n");
				return -EINVAL;
			}
			
			up3d_fpga_ctrl(ctx, 0);
			up3d_fpga_set_output_image_addr(ctx, dma_addr);
			up3d_fpga_ctrl(ctx, 1);
			dev_dbg(ctx->dev, "up3d_start_streaming: set output image address to 0x%x\n", dma_addr);
		}
		atomic_set(&ctx->device_info.status, UP3D_STA_RUN);
	}

	return 0;
}

static void up3d_stop_streaming(struct vb2_queue *q)
{
	struct up3d_vb2_buf *up3d_vb, *tmp;
	struct up3d_video_ctx *ctx = vb2_get_drv_priv(q);

	if (atomic_read(&ctx->device_info.status) == UP3D_STA_RUN)
	{
		gpiod_set_value_cansleep(ctx->completed_gpio, 0);
		msleep(10);
		// disable_irq_nosync(ctx->irq);	  // TODO: 后续应该是通过AXI-IIC通知FPGA停止产生中断
		disable_irq(ctx->irq);	  // TODO: 后续应该是通过AXI-IIC通知FPGA停止产生中断
		// atomic_set(&ctx->device_info.status, UP3D_STA_PAUSE); // note: 这里没有完全释放IRQ，只是暂停了中断，释放中断放到remove中
		atomic_set(&ctx->device_info.status, UP3D_STA_STOP); 

		unsigned long flags;

		spin_lock_irqsave(&ctx->vb_queue_lock, flags);
		if(ctx->current_vb) {
			vb2_buffer_done(&ctx->current_vb->vb.vb2_buf, VB2_BUF_STATE_ERROR);
			ctx->current_vb = NULL;
		}

		list_for_each_entry_safe(up3d_vb, tmp, &ctx->vb_queue_active, list) {
			list_del(&up3d_vb->list);
			vb2_buffer_done(&up3d_vb->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		}
		spin_unlock_irqrestore(&ctx->vb_queue_lock, flags);
		atomic_set(&ctx->device_info.irq_is_disable, 1);
	}
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
	dev_dbg(ctx->dev, "up3d_buf_init: Buffer %p, ctx %p\n", buf, ctx);
	INIT_LIST_HEAD(&buf->list);
	return 0;
}

static void up3d_buf_cleanup(struct vb2_buffer *vb)
{
}

int up3d_vb2_queue_init(struct vb2_queue *q, struct up3d_video_ctx *ctx)
{
	int ret = dma_set_mask_and_coherent(ctx->dev, DMA_BIT_MASK(32));
	if (ret){	
		dev_err(ctx->dev, "Failed to set DMA mask: %d\n", ret);
		return ret;
	}

	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->buf_struct_size = sizeof(struct up3d_vb2_buf);
	q->ops = &up3d_vb2_ops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &ctx->mutex;
	q->drv_priv = ctx;
	q->allow_cache_hints = 1;
	q->dev = ctx->dev;	// ！！！及其重要

	spin_lock_init(&ctx->vb_queue_lock);
	INIT_LIST_HEAD(&ctx->vb_queue_active);
	return vb2_queue_init(q);
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
