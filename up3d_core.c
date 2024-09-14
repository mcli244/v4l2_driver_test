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


#include <media/videobuf2-vmalloc.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-fh.h>

#include "up3d.h"
#include "up3d_ioctl.h"
#include "up3d_v4l2_fops.h"

#define VID_MODULE_NAME "up3d_vid"


// 默认格式
#define WIDTH_MAX	1920
#define HEIGHT_MAX	1080

#define WIDTH_DEF	640
#define HEIGHT_DEF	360

static struct up3d_video_ctx up3dvideo_ctx;

struct up3d_fmtdesc up3d_fmtdesc_lists[]=
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
	}
};

static void my_v4l2_release(struct v4l2_device *v4l2_dev)
{
	trace_in();
	trace_exit();
}


static int up3d_video_pdrv_probe(struct platform_device *pdev)
{
    int erron;
    int ret;
    struct video_device *vfd;

	trace_in();

	up3dvideo_ctx.dev = &pdev->dev;
    /* register v4l2_device */
    snprintf(up3dvideo_ctx.v4l2_dev.name, sizeof(up3dvideo_ctx.v4l2_dev.name), "%s-%03d", VID_MODULE_NAME, 0);
	ret = v4l2_device_register(&pdev->dev, &up3dvideo_ctx.v4l2_dev);
	if (ret) {
		return ret;
	}
	up3dvideo_ctx.v4l2_dev.release = my_v4l2_release;
	
	// capabilities信息
	strcpy(up3dvideo_ctx.cap.driver, "up3d_driver"); // 驱动名称
	strcpy(up3dvideo_ctx.cap.card, "up3d_device");   // 设备名称
	up3dvideo_ctx.cap.version = 0x0001;          // 版本号
	up3dvideo_ctx.cap.capabilities =	V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING 
									| V4L2_CAP_READWRITE | V4L2_CAP_DEVICE_CAPS;    // 能力，捕获和流 
	up3dvideo_ctx.cap.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
	up3dvideo_ctx.width_max = WIDTH_MAX;
	up3dvideo_ctx.height_max = HEIGHT_MAX;
	up3dvideo_ctx.width_def = WIDTH_DEF;
	up3dvideo_ctx.height_def = HEIGHT_DEF;
	up3dvideo_ctx.fmt_lists = &up3d_fmtdesc_lists[0];
	up3dvideo_ctx.fmt_lists_cnt = ARRAY_SIZE(up3d_fmtdesc_lists);

    // 视频设备操作
	mutex_init(&up3dvideo_ctx.mutex);
    vfd 				= &up3dvideo_ctx.vid_cap_dev;
    vfd->fops 			= &up3d_v4l2_fops;
    vfd->ioctl_ops 		= &up3d_v4l2_ioctl_ops;
    vfd->device_caps 	= up3dvideo_ctx.cap.device_caps;
    vfd->release 		= video_device_release_empty;
    vfd->v4l2_dev 		= &up3dvideo_ctx.v4l2_dev;
    vfd->queue 			= &up3dvideo_ctx.vb_queue;  
    vfd->tvnorms		= 0;   // 意义不明
    vfd->lock 			= &up3dvideo_ctx.mutex;    // 未v4l2设置锁，先没用上暂时不加
	snprintf(vfd->name, sizeof(vfd->name),  "up3d-%03d-vid-cap", 0);
	video_set_drvdata(vfd, &up3dvideo_ctx);
    erron = video_register_device(vfd, VFL_TYPE_VIDEO, -1);
    if(erron)
    {
        UP3D_DEBUG("video_register_device erron:%d ", erron);
        goto unreg_dev;
    }


	trace_exit();

    return 0;

unreg_dev:

    v4l2_device_put(&up3dvideo_ctx.v4l2_dev);
	trace_exit();
    return -ENOMEM;
}
static int up3d_video_pdrv_remove(struct platform_device *dev)
{
    trace_in();

    video_unregister_device(&up3dvideo_ctx.vid_cap_dev);
    vb2_queue_release(&up3dvideo_ctx.vb_queue);
    v4l2_device_put(&up3dvideo_ctx.v4l2_dev);
	
	trace_exit();
    return 0;
}

static void up3d_video_pdev_release(struct device *dev)
{
	trace_in();
	trace_exit();
}

static struct platform_device up3d_video_pdev = {
	.name		= "up3d_video_610",
	.dev.release	= up3d_video_pdev_release,
};

static struct platform_driver up3d_video_pdrv = {
	.probe		= up3d_video_pdrv_probe,
	.remove		= up3d_video_pdrv_remove,
	.driver		= {
		.name	= "up3d_video_610",
	},
};

static int __init up3d_video_610_init(void)
{
	int ret;
    trace_in();

	ret = platform_device_register(&up3d_video_pdev);
	if (ret)
		return ret;

	ret = platform_driver_register(&up3d_video_pdrv);
	if (ret)
		platform_device_unregister(&up3d_video_pdev);

	trace_exit();
	return ret;
}

static void __exit up3d_video_610_exit(void)
{
    trace_in();
	platform_driver_unregister(&up3d_video_pdrv);
	platform_device_unregister(&up3d_video_pdev);
	trace_exit();
}

module_init(up3d_video_610_init);
module_exit(up3d_video_610_exit);
MODULE_DESCRIPTION("Up3d 610 Video Driver");
MODULE_AUTHOR("CoreyLee");
MODULE_LICENSE("GPL");