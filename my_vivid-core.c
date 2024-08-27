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

// static struct vivid_dev *vivid_devs[VIVID_MAX_DEVS];

const struct v4l2_rect vivid_min_rect = {
	0, 0, MIN_WIDTH, MIN_HEIGHT
};

const struct v4l2_rect vivid_max_rect = {
	0, 0, MAX_WIDTH * MAX_ZOOM, MAX_HEIGHT * MAX_ZOOM
};

static struct vivid_dev *my_vivid_dev;
static struct v4l2_format my_v4l2_format;
static struct v4l2_fmtdesc my_fmtdesc_list[]=
{
	{
		.description = "5:6:5, RGB",
		.pixelformat = V4L2_PIX_FMT_RGB565,
	},
	{
		.description = "16  YUV 4:2:2",
		.pixelformat = V4L2_PIX_FMT_YUYV,
	}
};

static int my_vidioc_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	printk(KERN_WARNING "my_vidioc_querycap.");
    strcpy(cap->driver, "my_vdev"); // 驱动名称
	strcpy(cap->card, "my_vdev");   // 设备名称
	cap->version = 0x0001;          // 版本号
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;    // 能力，捕获和流 
	
  /*     物理设备 整体功能
  * @capabilities: capabilities of the physical device as a whole
    通过此特定设备（节点）访问的功能 
  * @device_caps:  capabilities accessed via this particular device (node)
  */
	cap->capabilities |=  V4L2_CAP_DEVICE_CAPS; // 4.15.0内核下需要设置，估计往上的版本也需要置位该标志
	
	return 0;
}

static int vivid_fop_release(struct file *file)
{
    printk(KERN_WARNING "vivid_fop_release.");
    return 0;
	// struct vivid_dev *dev = video_drvdata(file);
	// struct video_device *vdev = video_devdata(file);

	// mutex_lock(&dev->mutex);
	// if (!no_error_inj && v4l2_fh_is_singular_file(file) &&
	//     !video_is_registered(vdev) && vivid_is_last_user(dev)) {
	// 	/*
	// 	 * I am the last user of this driver, and a disconnect
	// 	 * was forced (since this video_device is unregistered),
	// 	 * so re-register all video_device's again.
	// 	 */
	// 	v4l2_info(&dev->v4l2_dev, "reconnect\n");
	// 	set_bit(V4L2_FL_REGISTERED, &dev->vid_cap_dev.flags);
	// 	set_bit(V4L2_FL_REGISTERED, &dev->vid_out_dev.flags);
	// 	set_bit(V4L2_FL_REGISTERED, &dev->vbi_cap_dev.flags);
	// 	set_bit(V4L2_FL_REGISTERED, &dev->vbi_out_dev.flags);
	// 	set_bit(V4L2_FL_REGISTERED, &dev->sdr_cap_dev.flags);
	// 	set_bit(V4L2_FL_REGISTERED, &dev->radio_rx_dev.flags);
	// 	set_bit(V4L2_FL_REGISTERED, &dev->radio_tx_dev.flags);
	// }
	// mutex_unlock(&dev->mutex);
	// if (file->private_data == dev->overlay_cap_owner)
	// 	dev->overlay_cap_owner = NULL;
	// if (file->private_data == dev->radio_rx_rds_owner) {
	// 	dev->radio_rx_rds_last_block = 0;
	// 	dev->radio_rx_rds_owner = NULL;
	// }
	// if (file->private_data == dev->radio_tx_rds_owner) {
	// 	dev->radio_tx_rds_last_block = 0;
	// 	dev->radio_tx_rds_owner = NULL;
	// }
	// if (vdev->queue)
	// 	return vb2_fop_release(file);
	// return v4l2_fh_release(file);
}

static const struct v4l2_file_operations myvivid_fops = {
	.owner		= THIS_MODULE,
	.open           = v4l2_fh_open,
	.release        = vivid_fop_release,
	.read           = vb2_fop_read,
	.write          = vb2_fop_write,
	.poll		= vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap           = vb2_fop_mmap,
};

/* 列举支持哪种格式 */
static int myvivi_vidioc_enum_fmt_vid_cap(struct file *file, void *fh,struct v4l2_fmtdesc *f)
{
	printk(KERN_WARNING "%s.", __FUNCTION__);
	if (f->index >= 2)	// 格式类型的索引，这里我们只支持一种格式，所以索引从0开始
		return -EINVAL;

	strcpy(f->description, my_fmtdesc_list[f->index].description);
	f->pixelformat = my_fmtdesc_list[f->index].pixelformat;

	return 0;
}

/* 获取当前使用的格式 */
static int myvivi_vidioc_g_fmt_vid_cap(struct file *file, void *fh,struct v4l2_format *f)
{
	printk(KERN_WARNING "%s.", __FUNCTION__);
	// memcpy(f, &my_v4l2_format, sizeof(my_v4l2_format));

	// 设置图像的宽、高
	f->fmt.pix.width = my_v4l2_format.fmt.pix.width;
	f->fmt.pix.height = my_v4l2_format.fmt.pix.height;
	f->fmt.pix.pixelformat = my_v4l2_format.fmt.pix.pixelformat;
	// 计算每行多少bit = width * 位深，赋值各成员变量struct usbvision_v4l2_format_st ---> int bytes_per_pixel;(1、2、3、4、2.....)
	f->fmt.pix.bytesperline = f->fmt.pix.width * 8;
	// 计算图像大小: 每行多少bit * height(行数) 单位：bit
	f->fmt.pix.sizeimage = f->fmt.pix.bytesperline * f->fmt.pix.height;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	f->fmt.pix.field = V4L2_FIELD_NONE; 
	return 0;
}


/* 尝试是否支持某种格式 */
static int myvivi_vidioc_try_fmt_vid_cap(struct file *file, void *fh,struct v4l2_format *f)
{
	unsigned int maxw, maxh;
    enum v4l2_field field;
	
	printk(KERN_WARNING "%s.", __FUNCTION__);
	if(f->fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV)
		return -EINVAL;
	
	field = f->fmt.pix.field;
	if (field == V4L2_FIELD_ANY) {
		field = V4L2_FIELD_INTERLACED;
	} else if (V4L2_FIELD_INTERLACED != field) {
		return -EINVAL;
	}

	maxw  = 1024;
	maxh  = 768;

    /* 调整format的width, height, 
     * 计算bytesperline, sizeimage
     */
	v4l_bound_align_image(&f->fmt.pix.width, 48, maxw, 2,
			      &f->fmt.pix.height, 32, maxh, 0, 0);
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * 16) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;

	return 0;
}

static int myvivi_vidioc_s_fmt_vid_cap(struct file *file, void *fh,struct v4l2_format *f)
{
	int ret;
	printk(KERN_WARNING "%s.", __FUNCTION__);
	ret = myvivi_vidioc_try_fmt_vid_cap(file, NULL, f);
	if (ret < 0)
		return ret;

    memcpy(&my_v4l2_format, f, sizeof(my_v4l2_format));
    
	return 0;
}

/* 枚举支持的输入设备 */
#define INPUT_DEVICE_NUMS	1
static int my_vidioc_enum_input(struct file *file, void *fh,struct v4l2_input *inp)
{
	if (inp->index >= INPUT_DEVICE_NUMS)	// 只支持一种输入设备，就是相机自身
		return -EINVAL;	
 
	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->std = V4L2_STD_525_60;
	sprintf(inp->name, "Camera %u", inp->index);
	return 0;
}

/* 获取输入设备 */
static int my_vidioc_g_input(struct file *file, void *fh, unsigned int *i)
{
	struct vivid_dev *dev = video_drvdata(file);
	*i = dev->input;
	return 0;
}

/* 设置输入设备 */
static int my_vidioc_s_input(struct file *file, void *fh, unsigned int i)
{
	struct vivid_dev *dev = video_drvdata(file);
 
	if (i >= INPUT_DEVICE_NUMS)
		return -EINVAL;
 
	dev->input = i;
	dev->vid_cap_dev.tvnorms = V4L2_STD_ALL;
	return 0;
}

const struct v4l2_ioctl_ops my_v4l2_ioctl_ops=
{
    // 表示它是一个摄像头设备
    .vidioc_querycap      = my_vidioc_querycap,

    /* 用于列举、获得、测试、设置摄像头的数据的格式 */
    .vidioc_enum_fmt_vid_cap  = myvivi_vidioc_enum_fmt_vid_cap,
    .vidioc_g_fmt_vid_cap     = myvivi_vidioc_g_fmt_vid_cap,
    .vidioc_try_fmt_vid_cap   = myvivi_vidioc_try_fmt_vid_cap,
    .vidioc_s_fmt_vid_cap     = myvivi_vidioc_s_fmt_vid_cap,
    
    /* 缓冲区操作: 申请/查询/放入队列/取出队列 使用video buffer提供的函数 */
	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,

	/* 输入设备相关 */
	.vidioc_enum_input		= my_vidioc_enum_input,
	.vidioc_g_input			= my_vidioc_g_input,
	.vidioc_s_input			= my_vidioc_s_input,

};

static void my_v4l2_release(struct v4l2_device *v4l2_dev)
{

}


static int my_vivid_probe(struct platform_device *pdev)
{
    int erron, i;
    int ret;
    struct vb2_queue *q;
    struct video_device *vfd;
	v4l2_std_id tvnorms_cap = 0, tvnorms_out = 0;
	static const struct v4l2_dv_timings def_dv_timings =
					V4L2_DV_BT_CEA_1280X720P60;
	unsigned in_type_counter[4] = { 0, 0, 0, 0 };
	unsigned out_type_counter[4] = { 0, 0, 0, 0 };
	int ccs_cap = ccs_cap_mode[0];
	int ccs_out = ccs_out_mode[0];

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

		ret = -ENOMEM;
	/* initialize the test pattern generator */
	tpg_init(&my_vivid_dev->tpg, 640, 360);
	if (tpg_alloc(&my_vivid_dev->tpg, MAX_ZOOM * MAX_WIDTH))
		goto free_dev;
	my_vivid_dev->scaled_line = vzalloc(MAX_ZOOM * MAX_WIDTH);
	if (!my_vivid_dev->scaled_line)
		goto free_dev;
	my_vivid_dev->blended_line = vzalloc(MAX_ZOOM * MAX_WIDTH);
	if (!my_vivid_dev->blended_line)
		goto free_dev;

	/* load the edid */
	my_vivid_dev->edid = vmalloc(256 * 128);
	if (!my_vivid_dev->edid)
		goto free_dev;

	/* create a string array containing the names of all the preset timings */
	while (v4l2_dv_timings_presets[my_vivid_dev->query_dv_timings_size].bt.width)
		my_vivid_dev->query_dv_timings_size++;
	my_vivid_dev->query_dv_timings_qmenu = kmalloc(my_vivid_dev->query_dv_timings_size *
					   (sizeof(void *) + 32), GFP_KERNEL);
	if (my_vivid_dev->query_dv_timings_qmenu == NULL)
		goto free_dev;
	for (i = 0; i < my_vivid_dev->query_dv_timings_size; i++) {
		const struct v4l2_bt_timings *bt = &v4l2_dv_timings_presets[i].bt;
		char *p = (char *)&my_vivid_dev->query_dv_timings_qmenu[my_vivid_dev->query_dv_timings_size];
		u32 htot, vtot;

		p += i * 32;
		my_vivid_dev->query_dv_timings_qmenu[i] = p;

		htot = V4L2_DV_BT_FRAME_WIDTH(bt);
		vtot = V4L2_DV_BT_FRAME_HEIGHT(bt);
		snprintf(p, 32, "%ux%u%s%u",
			bt->width, bt->height, bt->interlaced ? "i" : "p",
			(u32)bt->pixelclock / (htot * vtot));
	}

    /* set up the capabilities of the video capture device */
    my_vivid_dev->vid_cap_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
    
    printk(KERN_WARNING "my_vivid_probe.2");
	my_vivid_dev->fmt_cap = &vivid_formats[0];
	my_vivid_dev->fmt_out = &vivid_formats[0];
	if (!my_vivid_dev->multiplanar)
		vivid_formats[0].data_offset[0] = 0;
	my_vivid_dev->webcam_size_idx = 1;
	my_vivid_dev->webcam_ival_idx = 3;
	tpg_s_fourcc(&my_vivid_dev->tpg, my_vivid_dev->fmt_cap->fourcc);
	my_vivid_dev->std_cap = V4L2_STD_PAL;
	my_vivid_dev->std_out = V4L2_STD_PAL;
	if (my_vivid_dev->input_type[0] == TV || my_vivid_dev->input_type[0] == SVID)
		tvnorms_cap = V4L2_STD_ALL;
	if (my_vivid_dev->output_type[0] == SVID)
		tvnorms_out = V4L2_STD_ALL;
	my_vivid_dev->dv_timings_cap = def_dv_timings;
	my_vivid_dev->dv_timings_out = def_dv_timings;

	ret = vivid_create_controls(my_vivid_dev, ccs_cap == -1, ccs_out == -1, no_error_inj,
			in_type_counter[TV] || in_type_counter[SVID] ||
			out_type_counter[SVID],
			in_type_counter[HDMI] || out_type_counter[HDMI]);
	if (ret)
		goto unreg_dev;

	/*
	 * update the capture and output formats to do a proper initial
	 * configuration.
	 */
	vivid_update_format_cap(my_vivid_dev, false);
	vivid_update_format_out(my_vivid_dev);
	v4l2_ctrl_handler_setup(&my_vivid_dev->ctrl_hdl_vid_cap);

	printk(KERN_WARNING "my_vivid_probe.123");
	mutex_init(&my_vivid_dev->mutex);
	spin_lock_init(&my_vivid_dev->slock);
	/* init dma queues */
	INIT_LIST_HEAD(&my_vivid_dev->vid_cap_active);

    printk(KERN_WARNING "my_vivid_probe.3");
    // 初始化一个队列
    q = &my_vivid_dev->vb_vid_cap_q;
    q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  // 类型是视频捕获设备
    q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF | VB2_READ; // 仅支持应用层做mmap映射的方式
	q->drv_priv = my_vivid_dev;
    q->buf_struct_size = sizeof(struct vivid_buffer);
    q->ops = &vivid_vid_cap_qops;   // 操作接口用vivid提供的，位于vivid-vid-cap.c
    q->mem_ops = &vb2_vmalloc_memops;   // videobuf2_vmalloc.h
    q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC; // 尚不明确
    q->min_buffers_needed = 2;
    q->lock = &my_vivid_dev->mutex;
    q->dev = my_vivid_dev->v4l2_dev.dev;
    ret = vb2_queue_init(q);
    if (ret)
	{
		goto unreg_dev;
	}

    printk(KERN_WARNING "my_vivid_probe.4");
    // 视频设备操作
    vfd = &my_vivid_dev->vid_cap_dev;
    snprintf(vfd->name, sizeof(vfd->name),  "vivid-%03d-vid-cap", 0);
    vfd->fops = &myvivid_fops;
    vfd->ioctl_ops = &my_v4l2_ioctl_ops;
    vfd->device_caps = my_vivid_dev->vid_cap_caps;
    vfd->release = video_device_release_empty;
    vfd->v4l2_dev = &my_vivid_dev->v4l2_dev;
    vfd->queue = &my_vivid_dev->vb_vid_cap_q;   // 等待初始化
    vfd->tvnorms = 0;   // 意义不明
    vfd->lock = &my_vivid_dev->mutex;    // 未v4l2设置锁，先没用上暂时不加
	video_set_drvdata(vfd, my_vivid_dev);
    erron = video_register_device(vfd, VFL_TYPE_GRABBER, -1);
    if(erron)
    {
        printk(KERN_WARNING "video_register_device erron:%d ", erron);
        goto unreg_dev;
    }

    printk(KERN_WARNING "my_vivid_probe.6");
    return 0;

free_dev:
unreg_dev:

    printk(KERN_WARNING "my_vivid_probe error");
    v4l2_device_put(&my_vivid_dev->v4l2_dev);
    kfree(my_vivid_dev);

    return -ENOMEM;
}
static int my_vivid_remove(struct platform_device *dev)
{
    printk(KERN_WARNING "my_vivid_remove.");

    video_unregister_device(&my_vivid_dev->vid_cap_dev);
    vb2_queue_release(&my_vivid_dev->vb_vid_cap_q);
    v4l2_device_put(&my_vivid_dev->v4l2_dev);
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
