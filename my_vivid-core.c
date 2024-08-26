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

#define VIVID_MODULE_NAME "myvivid"

/* The maximum number of vivid devices */
#define VIVID_MAX_DEVS CONFIG_VIDEO_VIVID_MAX_DEVS

MODULE_DESCRIPTION("Virtual Video Test Driver");
MODULE_AUTHOR("Hans Verkuil");
MODULE_LICENSE("GPL");

static unsigned n_devs = 1;
module_param(n_devs, uint, 0444);
MODULE_PARM_DESC(n_devs, " number of driver instances to create");

static int vid_cap_nr[VIVID_MAX_DEVS] = { [0 ... (VIVID_MAX_DEVS - 1)] = -1 };
module_param_array(vid_cap_nr, int, NULL, 0444);
MODULE_PARM_DESC(vid_cap_nr, " videoX start number, -1 is autodetect");

static int vid_out_nr[VIVID_MAX_DEVS] = { [0 ... (VIVID_MAX_DEVS - 1)] = -1 };
module_param_array(vid_out_nr, int, NULL, 0444);
MODULE_PARM_DESC(vid_out_nr, " videoX start number, -1 is autodetect");

static int vbi_cap_nr[VIVID_MAX_DEVS] = { [0 ... (VIVID_MAX_DEVS - 1)] = -1 };
module_param_array(vbi_cap_nr, int, NULL, 0444);
MODULE_PARM_DESC(vbi_cap_nr, " vbiX start number, -1 is autodetect");

static int vbi_out_nr[VIVID_MAX_DEVS] = { [0 ... (VIVID_MAX_DEVS - 1)] = -1 };
module_param_array(vbi_out_nr, int, NULL, 0444);
MODULE_PARM_DESC(vbi_out_nr, " vbiX start number, -1 is autodetect");

static int sdr_cap_nr[VIVID_MAX_DEVS] = { [0 ... (VIVID_MAX_DEVS - 1)] = -1 };
module_param_array(sdr_cap_nr, int, NULL, 0444);
MODULE_PARM_DESC(sdr_cap_nr, " swradioX start number, -1 is autodetect");

static int radio_rx_nr[VIVID_MAX_DEVS] = { [0 ... (VIVID_MAX_DEVS - 1)] = -1 };
module_param_array(radio_rx_nr, int, NULL, 0444);
MODULE_PARM_DESC(radio_rx_nr, " radioX start number, -1 is autodetect");

static int radio_tx_nr[VIVID_MAX_DEVS] = { [0 ... (VIVID_MAX_DEVS - 1)] = -1 };
module_param_array(radio_tx_nr, int, NULL, 0444);
MODULE_PARM_DESC(radio_tx_nr, " radioX start number, -1 is autodetect");

static int ccs_cap_mode[VIVID_MAX_DEVS] = { [0 ... (VIVID_MAX_DEVS - 1)] = -1 };
module_param_array(ccs_cap_mode, int, NULL, 0444);
MODULE_PARM_DESC(ccs_cap_mode, " capture crop/compose/scale mode:\n"
			   "\t\t    bit 0=crop, 1=compose, 2=scale,\n"
			   "\t\t    -1=user-controlled (default)");

static int ccs_out_mode[VIVID_MAX_DEVS] = { [0 ... (VIVID_MAX_DEVS - 1)] = -1 };
module_param_array(ccs_out_mode, int, NULL, 0444);
MODULE_PARM_DESC(ccs_out_mode, " output crop/compose/scale mode:\n"
			   "\t\t    bit 0=crop, 1=compose, 2=scale,\n"
			   "\t\t    -1=user-controlled (default)");

static unsigned multiplanar[VIVID_MAX_DEVS] = { [0 ... (VIVID_MAX_DEVS - 1)] = 1 };
module_param_array(multiplanar, uint, NULL, 0444);
MODULE_PARM_DESC(multiplanar, " 1 (default) creates a single planar device, 2 creates a multiplanar device.");

/* Default: video + vbi-cap (raw and sliced) + radio rx + radio tx + sdr + vbi-out + vid-out */
static unsigned node_types[VIVID_MAX_DEVS] = { [0 ... (VIVID_MAX_DEVS - 1)] = 0x1d3d };
module_param_array(node_types, uint, NULL, 0444);
MODULE_PARM_DESC(node_types, " node types, default is 0x1d3d. Bitmask with the following meaning:\n"
			     "\t\t    bit 0: Video Capture node\n"
			     "\t\t    bit 2-3: VBI Capture node: 0 = none, 1 = raw vbi, 2 = sliced vbi, 3 = both\n"
			     "\t\t    bit 4: Radio Receiver node\n"
			     "\t\t    bit 5: Software Defined Radio Receiver node\n"
			     "\t\t    bit 8: Video Output node\n"
			     "\t\t    bit 10-11: VBI Output node: 0 = none, 1 = raw vbi, 2 = sliced vbi, 3 = both\n"
			     "\t\t    bit 12: Radio Transmitter node\n"
			     "\t\t    bit 16: Framebuffer for testing overlays");

/* Default: 4 inputs */
static unsigned num_inputs[VIVID_MAX_DEVS] = { [0 ... (VIVID_MAX_DEVS - 1)] = 4 };
module_param_array(num_inputs, uint, NULL, 0444);
MODULE_PARM_DESC(num_inputs, " number of inputs, default is 4");

/* Default: input 0 = WEBCAM, 1 = TV, 2 = SVID, 3 = HDMI */
static unsigned input_types[VIVID_MAX_DEVS] = { [0 ... (VIVID_MAX_DEVS - 1)] = 0xe4 };
module_param_array(input_types, uint, NULL, 0444);
MODULE_PARM_DESC(input_types, " input types, default is 0xe4. Two bits per input,\n"
			      "\t\t    bits 0-1 == input 0, bits 31-30 == input 15.\n"
			      "\t\t    Type 0 == webcam, 1 == TV, 2 == S-Video, 3 == HDMI");

/* Default: 2 outputs */
static unsigned num_outputs[VIVID_MAX_DEVS] = { [0 ... (VIVID_MAX_DEVS - 1)] = 2 };
module_param_array(num_outputs, uint, NULL, 0444);
MODULE_PARM_DESC(num_outputs, " number of outputs, default is 2");

/* Default: output 0 = SVID, 1 = HDMI */
static unsigned output_types[VIVID_MAX_DEVS] = { [0 ... (VIVID_MAX_DEVS - 1)] = 2 };
module_param_array(output_types, uint, NULL, 0444);
MODULE_PARM_DESC(output_types, " output types, default is 0x02. One bit per output,\n"
			      "\t\t    bit 0 == output 0, bit 15 == output 15.\n"
			      "\t\t    Type 0 == S-Video, 1 == HDMI");

unsigned vivid_debug;
module_param(vivid_debug, uint, 0644);
MODULE_PARM_DESC(vivid_debug, " activates debug info");

static bool no_error_inj;
module_param(no_error_inj, bool, 0444);
MODULE_PARM_DESC(no_error_inj, " if set disable the error injecting controls");

static unsigned int allocators[VIVID_MAX_DEVS] = { [0 ... (VIVID_MAX_DEVS - 1)] = 0 };
module_param_array(allocators, uint, NULL, 0444);
MODULE_PARM_DESC(allocators, " memory allocator selection, default is 0.\n"
			     "\t\t    0 == vmalloc\n"
			     "\t\t    1 == dma-contig");

static struct vivid_dev *vivid_devs[VIVID_MAX_DEVS];

const struct v4l2_rect vivid_min_rect = {
	0, 0, MIN_WIDTH, MIN_HEIGHT
};

const struct v4l2_rect vivid_max_rect = {
	0, 0, MAX_WIDTH * MAX_ZOOM, MAX_HEIGHT * MAX_ZOOM
};
static struct video_device *my_video_dev;
struct vivid_dev *my_vivid_dev;

static int my_vidioc_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
    strcpy(cap->driver, "my_vdev"); // 驱动名称
	strcpy(cap->card, "my_vdev");   // 设备名称
	cap->version = 0x0001;          // 版本号
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;    // 能力，捕获和流
	return 0;
}

const struct v4l2_ioctl_ops my_v4l2_ioctl_ops=
{
    // 表示它是一个摄像头设备
    .vidioc_querycap      = my_vidioc_querycap,

    // /* 用于列举、获得、测试、设置摄像头的数据的格式 */
    // .vidioc_enum_fmt_vid_cap  = myvivi_vidioc_enum_fmt_vid_cap,
    // .vidioc_g_fmt_vid_cap     = myvivi_vidioc_g_fmt_vid_cap,
    // .vidioc_try_fmt_vid_cap   = myvivi_vidioc_try_fmt_vid_cap,
    // .vidioc_s_fmt_vid_cap     = myvivi_vidioc_s_fmt_vid_cap,
    
    // /* 缓冲区操作: 申请/查询/放入队列/取出队列 */
    // .vidioc_reqbufs       = myvivi_vidioc_reqbufs,
    // .vidioc_querybuf      = myvivi_vidioc_querybuf,
    // .vidioc_qbuf          = myvivi_vidioc_qbuf,
    // .vidioc_dqbuf         = myvivi_vidioc_dqbuf,
    
    // // 启动/停止
    // .vidioc_streamon      = myvivi_vidioc_streamon,
    // .vidioc_streamoff     = myvivi_vidioc_streamoff, 
};

static void my_v4l2_release(struct v4l2_device *v4l2_dev)
{

}
static int my_vivid_probe(struct platform_device *pdev)
{
    int erron;
    int ret;
    struct vb2_queue *q;
    struct video_device *vfd;

    printk(KERN_WARNING "my_vivid_probe.");

    my_vivid_dev = kzalloc(sizeof(*my_vivid_dev), GFP_KERNEL);
	if (!my_vivid_dev)
		return -ENOMEM;

    printk(KERN_WARNING "my_vivid_probe.1");
    /* register v4l2_device */
    snprintf(my_vivid_dev->v4l2_dev.name, sizeof(my_vivid_dev->v4l2_dev.name), "%s-%03d", VIVID_MODULE_NAME, 0);
	ret = v4l2_device_register(&pdev->dev, &my_vivid_dev->v4l2_dev);
	if (ret) {
		kfree(my_vivid_dev);
		return ret;
	}
	my_vivid_dev->v4l2_dev.release = my_v4l2_release;

    /* set up the capabilities of the video capture device */
    my_vivid_dev->vid_cap_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
    
    printk(KERN_WARNING "my_vivid_probe.2");
    mutex_init(&my_vivid_dev->mutex);

    printk(KERN_WARNING "my_vivid_probe.3");
    // 初始化一个队列
    q = &my_vivid_dev->vb_vid_cap_q;
    q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  // 类型是视频捕获设备
    q->io_modes = VB2_MMAP; // 仅支持应用层做mmap映射的方式
    q->buf_struct_size = sizeof(struct vivid_buffer);
    q->ops = &vivid_vid_cap_qops;   // 操作接口用vivid提供的，位于vivid-vid-cap.c
    q->mem_ops = &vb2_vmalloc_memops;   // videobuf2_vmalloc.h
    q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC; // 尚不明确
    q->min_buffers_needed = 2;
    q->lock = &my_vivid_dev->mutex;
    q->dev = my_vivid_dev->v4l2_dev.dev;
    ret = vb2_queue_init(q);
    if (ret)
        goto unreg_dev;
    
    printk(KERN_WARNING "my_vivid_probe.4");
    // 视频设备操作
    vfd = &my_vivid_dev->vid_cap_dev;
    snprintf(vfd->name, sizeof(vfd->name),  "vivid-%03d-vid-cap", 0);
    vfd->ioctl_ops = &my_v4l2_ioctl_ops;
    vfd->device_caps = my_vivid_dev->vid_cap_caps;
    vfd->release = video_device_release_empty;
    vfd->v4l2_dev = &my_vivid_dev->v4l2_dev;
    vfd->queue = &my_vivid_dev->vb_vid_cap_q;   // 等待初始化
    vfd->tvnorms = 0;   // 意义不明
    // vfd->lock = &dev->mutex;    // 未v4l2设置锁，先没用上暂时不加
    erron = video_register_device(my_video_dev, VFL_TYPE_GRABBER, -1);

    printk(KERN_WARNING "my_vivid_probe.5");
    return erron;
unreg_dev:
    return -ENOMEM;
}
static int my_vivid_remove(struct platform_device *dev)
{
    printk(KERN_WARNING "my_vivid_remove.");

    video_unregister_device(my_video_dev);
    vb2_queue_release(&my_vivid_dev->vb_vid_cap_q);
    v4l2_device_unregister(&my_vivid_dev->v4l2_dev);
    kfree(my_vivid_dev);

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
