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
#include <media/v4l2-event.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-fh.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/of_reserved_mem.h>

#include "up3d.h"
#include "up3d_ioctl.h"
#include "up3d_vb2ops.h"

#define VID_MODULE_NAME "up3d_vid"

static struct up3d_video_ctx up3dvideo_ctx;
struct up3d_fmtdesc up3d_fmtdesc_lists[] =
	{
		{
			.description = "8:8:8, RGB",
			.pixel_format = V4L2_PIX_FMT_RGB24,
			.bytes_per_pixel = 3,
			.framesize.width = WIDTH_DEF,
			.framesize.height = HEIGHT_DEF,
		},
		{
			.description = "5:6:5, RGB",
			.pixel_format = V4L2_PIX_FMT_RGB565,
			.bytes_per_pixel = 1,
			.framesize.width = WIDTH_DEF,
			.framesize.height = HEIGHT_DEF,
		},
		{
			.description = "16  YUV 4:2:2",
			.pixel_format = V4L2_PIX_FMT_YUYV,
			.bytes_per_pixel = 1,
			.framesize.width = WIDTH_DEF,
			.framesize.height = HEIGHT_DEF,
		},
		{
			.description = "8bit  GREY",
			.pixel_format = V4L2_PIX_FMT_GREY,
			.bytes_per_pixel = 1,
			.framesize.width = 416,
			.framesize.height = 480,
		}};

static void my_v4l2_release(struct v4l2_device *v4l2_dev)
{
}

static int _up3d_reserved_memory_by_dtb(struct up3d_video_ctx *ctx, struct platform_device *pdev)
{
	/*
		reserved-memory {  // ✅ 必须在根节点下
			#address-cells = <1>;
			#size-cells = <1>;
			ranges;

			reserved: buffer@10000000 {
				compatible = "shared-dma-pool";
				reg = <0x10000000 0x01000000>;  // 物理地址 0x10000000，大小 16MB
				no-map;
				// 图像宽度 图像高度 每个像素占用字节数
				img-info = <416 480 1>;
				// 起始地址 间隔大小 总数量
				img-buffers = <0 0x100000 15>;
			};
		};

		pl_cap_intc: cap-intc@0 {
			compatible = "up3d610,cap-intc";
			status = "okay";
			interrupt-names = "pl-cap-intc";
			interrupt-parent = <&intc>;
			interrupts = <0 32 IRQ_TYPE_EDGE_RISING>;
			memory-region = <&reserved>;
		};
	*/
	struct resource res;
	phys_addr_t phys_addr;
	size_t size;
	const __be32 *prop;
	u32 img_width, img_height, img_bpp, img_bytes;
	u32 offset, blk_size, blk_count;
	int i;

	struct device *dev = &pdev->dev;
	struct device_node *np = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!np)
	{
		dev_err(dev, "Failed to parse memory-region\n");
		return -ENOMEM;
	}

	if (of_address_to_resource(np, 0, &res))
	{
		dev_err(dev, "Failed to get reserved memory resource\n");
		of_node_put(np);
		return -EINVAL;
	}

	phys_addr = res.start;
	size = resource_size(&res);

	dev_info(dev, "Reserved memory at phys_addr: 0x%llx, size: 0x%zx\n",
			 (unsigned long long)phys_addr, size);

	ctx->ddr_addr = memremap(phys_addr, size, MEMREMAP_WB);
	if (!ctx->ddr_addr)
	{
		dev_err(dev, "Failed to memremap DDR address\n");
		of_node_put(np);
		return -ENOMEM;
	}
	dev_info(dev, "Mapped reserved memory to virtual address: 0x%px - 0x%px  %#x\n",
			 ctx->ddr_addr, ctx->ddr_addr + size - 1, (int)ctx->ddr_addr);

	prop = of_get_property(np, "img-info", NULL);
	if (prop)
	{
		img_width = be32_to_cpu(prop[0]);
		img_height = be32_to_cpu(prop[1]);
		img_bpp = be32_to_cpu(prop[2]);
		img_bytes = img_width * img_height * img_bpp;
		pr_info("Image info: width=%u, height=%u, bpp=%u, bytes=%u\n",
				img_width, img_height, img_bpp, img_bytes);
	}
	else
	{
		dev_err(dev, "Failed to get image info\n");
		memunmap(ctx->ddr_addr);
		of_node_put(np);
		return -EINVAL;
	}

	prop = of_get_property(np, "img-buffers", NULL);
	if (prop)
	{
		offset = be32_to_cpu(prop[0]);
		blk_size = be32_to_cpu(prop[1]);
		blk_count = be32_to_cpu(prop[2]);
		if (ctx->ddr_addr + offset + blk_size * blk_count > ctx->ddr_addr + size ||
			blk_count == 0 || blk_count > MAX_IMAGE_BUFFER_COUNT)
		{
			dev_err(dev, "Image buffer exceeds reserved memory size! offset=%u, blk_size=%u, blk_count=%u\n",
					offset, blk_size, blk_count);
			memunmap(ctx->ddr_addr);
			of_node_put(np);
			return -EINVAL;
		}

		pr_info("Image buffers: offset=%u, blk_size=%u, blk_count=%u\n",
				offset, blk_size, blk_count);

		for (i = 0; i < blk_count; i++)
		{
			ctx->img_addrs[i] = ctx->ddr_addr + offset + (i * blk_size);
			pr_info("Image buffer %02d address: %px\n", i, ctx->img_addrs[i]);
		}
		ctx->img_blk_count = blk_count;
	}
	else
	{
		dev_err(dev, "Failed to get image buffers\n");
		memunmap(ctx->ddr_addr);
		of_node_put(np);
		return -EINVAL;
	}
	of_node_put(np);

	return 0;
}

static int _vb_queue_init(struct vb2_queue *q, struct up3d_video_ctx *ctx)
{
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP;
	q->buf_struct_size = sizeof(struct up3d_vb2_buf);
	q->ops = &up3d_vb2_ops,
	q->mem_ops = &vb2_vmalloc_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_buffers_needed = 2;
	q->lock = &ctx->mutex;
	q->drv_priv = ctx;

	spin_lock_init(&ctx->vb_queue_lock);
	INIT_LIST_HEAD(&ctx->vb_queue_active);

	return vb2_queue_init(q);
}

static int _init_format(struct v4l2_format *f, struct up3d_video_ctx *ctx)
{
	f->fmt.pix.width = ctx->width_def;
	f->fmt.pix.height = ctx->height_def;
	f->fmt.pix.pixelformat = ctx->fmt_lists[0].pixel_format;
	f->fmt.pix.bytesperline = f->fmt.pix.width * ctx->fmt_lists[0].bytes_per_pixel;
	f->fmt.pix.sizeimage = f->fmt.pix.bytesperline * f->fmt.pix.height;

	return 0;
}

const struct v4l2_file_operations up3d_v4l2_fops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
};

static int up3d_video_pdrv_probe(struct platform_device *pdev)
{
	int erron;
	int ret;
	struct video_device *vfd;

	memset(&up3dvideo_ctx, 0, sizeof(up3dvideo_ctx));
	up3dvideo_ctx.irq = platform_get_irq(pdev, 0);
	if (up3dvideo_ctx.irq < 0)
	{
		dev_err(&pdev->dev, "Failed to get IRQ\n");
		return up3dvideo_ctx.irq;
	}
	dev_info(&pdev->dev, "PL CAP INTC IRQ: %d\n", up3dvideo_ctx.irq);

	if (_up3d_reserved_memory_by_dtb(&up3dvideo_ctx, pdev) < 0)
	{
		dev_err(&pdev->dev, "Failed to memremap DDR address\n");
		goto irq_ext;
	}

	up3dvideo_ctx.dev = &pdev->dev;
	/* register v4l2_device */
	snprintf(up3dvideo_ctx.v4l2_dev.name, sizeof(up3dvideo_ctx.v4l2_dev.name), "%s-%03d", VID_MODULE_NAME, 0);
	ret = v4l2_device_register(&pdev->dev, &up3dvideo_ctx.v4l2_dev);
	if (ret < 0)
	{
		dev_err(&pdev->dev, "v4l2_device_register failed ret:%d ", ret);
		goto reserved_memory_free_ext;
	}
	up3dvideo_ctx.v4l2_dev.release = my_v4l2_release;

	strcpy(up3dvideo_ctx.cap.driver, "up3d_driver");
	strcpy(up3dvideo_ctx.cap.card, "up3d_device");
	up3dvideo_ctx.cap.version = 0x0001;
	up3dvideo_ctx.cap.capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_DEVICE_CAPS;
	up3dvideo_ctx.cap.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	up3dvideo_ctx.width_max = WIDTH_MAX;
	up3dvideo_ctx.height_max = HEIGHT_MAX;
	up3dvideo_ctx.width_def = WIDTH_DEF;
	up3dvideo_ctx.height_def = HEIGHT_DEF;
	up3dvideo_ctx.fmt_lists = &up3d_fmtdesc_lists[0];
	up3dvideo_ctx.fmt_lists_cnt = ARRAY_SIZE(up3d_fmtdesc_lists);

	_init_format(&up3dvideo_ctx.cur_v4l2_format, &up3dvideo_ctx);
	_vb_queue_init(&up3dvideo_ctx.vb_queue, &up3dvideo_ctx);

	mutex_init(&up3dvideo_ctx.mutex);
	vfd = &up3dvideo_ctx.vid_cap_dev;
	vfd->fops = &up3d_v4l2_fops;
	vfd->ioctl_ops = &up3d_v4l2_ioctl_ops;
	vfd->device_caps = up3dvideo_ctx.cap.device_caps;
	vfd->release = video_device_release_empty;
	vfd->v4l2_dev = &up3dvideo_ctx.v4l2_dev;
	vfd->queue = &up3dvideo_ctx.vb_queue;
	vfd->tvnorms = 0;
	vfd->lock = &up3dvideo_ctx.mutex;
	snprintf(vfd->name, sizeof(vfd->name), "up3d-%03d-vid-cap", 0);
	video_set_drvdata(vfd, &up3dvideo_ctx);
	erron = video_register_device(vfd, VFL_TYPE_VIDEO, -1);
	if (erron)
	{
		dev_err(&pdev->dev, "video_register_device erron:%d ", erron);
		goto unreg_dev;
	}

	return 0;

unreg_dev:
	v4l2_device_put(&up3dvideo_ctx.v4l2_dev);

reserved_memory_free_ext:
	memunmap(up3dvideo_ctx.ddr_addr);

irq_ext:
	devm_free_irq(&pdev->dev, platform_get_irq(pdev, 0), NULL);

	return -ENOMEM;
}
static int up3d_video_pdrv_remove(struct platform_device *dev)
{
	memunmap(up3dvideo_ctx.ddr_addr);
	devm_free_irq(&dev->dev, platform_get_irq(dev, 0), NULL);
	video_unregister_device(&up3dvideo_ctx.vid_cap_dev);
	v4l2_device_put(&up3dvideo_ctx.v4l2_dev);

	return 0;
}

static const struct of_device_id pl_cap_intc_of_match[] = {
	{.compatible = "up3d610,cap-intc"},
	{/* sentinel */}};
MODULE_DEVICE_TABLE(of, pl_cap_intc_of_match);

static struct platform_driver pl_cap_intc_driver = {
	.probe = up3d_video_pdrv_probe,
	.remove = up3d_video_pdrv_remove,
	.driver = {
		.name = "up3d_video_610",
		.of_match_table = pl_cap_intc_of_match,
	},
};

module_platform_driver(pl_cap_intc_driver);

MODULE_AUTHOR("CoreyLee <lixiangjun@up3dtech.com>");
MODULE_DESCRIPTION("Up3d610w Video Driver");
MODULE_LICENSE("GPL");
