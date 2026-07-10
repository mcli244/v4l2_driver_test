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
#include <linux/of_irq.h>
#include <linux/irq.h>

#include "up3d.h"
#include "up3d_ioctl.h"
#include "up3d_vb2ops.h"
#include "up3d_sysfs.h"
#include "up3d_fpga.h"

#define VID_MODULE_NAME "up3d_vid"

static struct up3d_video_ctx up3dvideo_ctx;

static void my_v4l2_release(struct v4l2_device *v4l2_dev)
{
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
	up3dvideo_ctx.dev = &pdev->dev;
	up3dvideo_ctx.pdev = pdev;

	/* register v4l2_device */
	snprintf(up3dvideo_ctx.v4l2_dev.name, sizeof(up3dvideo_ctx.v4l2_dev.name), "%s-%03d", VID_MODULE_NAME, 0);
	ret = v4l2_device_register(&pdev->dev, &up3dvideo_ctx.v4l2_dev);
	if (ret < 0)
	{
		dev_err(&pdev->dev, "v4l2_device_register failed ret:%d ", ret);
		return ret;
	}
	up3dvideo_ctx.v4l2_dev.release = my_v4l2_release;

	up3d_init_current_format(&up3dvideo_ctx);

	if(up3d_vb2_queue_init(&up3dvideo_ctx.vb_queue, &up3dvideo_ctx) < 0){
		dev_err(&pdev->dev, "Failed to initialize VB queue\n");
		goto unreg_dev;
	}

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

	ret = up3d_fpga_init(&up3dvideo_ctx);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to initialize fpga\n");
		goto unreg_dev;
	}

	/* 在 sysfs 创建属性组 */
	platform_set_drvdata(pdev, &up3dvideo_ctx);
    ret = sysfs_create_group(&pdev->dev.kobj, &up3d_attr_group);
    if (ret) {
        dev_err(&pdev->dev, "sysfs_create_group erron:%d ", erron);
		goto unreg_video;
    }

	up3dvideo_ctx.debugfs_root = debugfs_create_dir("up3d_video", NULL);
	if(up3dvideo_ctx.debugfs_root == NULL)
	{
		dev_err(&pdev->dev, "Failed to create debugfs directory\n");
		goto sysfs_remove;
	}
	debugfs_create_file("irq_count", 0444, up3dvideo_ctx.debugfs_root, &up3dvideo_ctx, &up3d_video_debugfs_fops);

	return 0;

sysfs_remove:
	sysfs_remove_group(&pdev->dev.kobj, &up3d_attr_group);

unreg_video:
	video_unregister_device(&up3dvideo_ctx.vid_cap_dev);

unreg_dev:
	v4l2_device_put(&up3dvideo_ctx.v4l2_dev);

	return -ENOMEM;
}
static void up3d_video_pdrv_remove(struct platform_device *dev)
{
	struct up3d_video_ctx *ctx = platform_get_drvdata(dev);

	if (!ctx)
		return;

	up3d_fpga_exit(ctx);

	debugfs_remove_recursive(ctx->debugfs_root);
    ctx->debugfs_root = NULL;
	sysfs_remove_group(&dev->dev.kobj, &up3d_attr_group);
	video_unregister_device(&ctx->vid_cap_dev);
	v4l2_device_put(&ctx->v4l2_dev);
}

static const struct of_device_id pl_cap_intc_of_match[] = {
	{.compatible = "up3d,up800w-video"},
	{/* sentinel */}};
MODULE_DEVICE_TABLE(of, pl_cap_intc_of_match);

static struct platform_driver pl_cap_intc_driver = {
	.probe = up3d_video_pdrv_probe,
	.remove = up3d_video_pdrv_remove,
	.driver = {
		.name = "up3d_up800w",
		.of_match_table = pl_cap_intc_of_match,
	},
};

module_platform_driver(pl_cap_intc_driver);

MODULE_AUTHOR("CoreyLee <lixiangjun@up3dtech.com>");
MODULE_DESCRIPTION("Up3d800w Video Driver");
MODULE_LICENSE("GPL");
