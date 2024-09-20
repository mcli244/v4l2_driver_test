#include "up3d_ioctl.h"
#include "up3d.h"



/* 列举支持哪种格式 */
static int up3d_enum_fmt_vid_cap(struct file *file, void *fh,struct v4l2_fmtdesc *f)
{
	struct up3d_video_ctx *ctx = video_drvdata(file);

	trace_in();
	
	if (f->index >= ctx->fmt_lists_cnt)	
		return -EINVAL;

	strcpy(f->description, ctx->fmt_lists[f->index].description);
	f->pixelformat = ctx->fmt_lists[f->index].pixel_format;

	trace_exit();
	return 0;
}

/* 获取当前使用的格式 */
static int up3d_g_fmt_vid_cap(struct file *file, void *fh,struct v4l2_format *f)
{
	struct up3d_video_ctx *ctx = video_drvdata(file);

	trace_in();
	memcpy(f, &ctx->cur_v4l2_format, sizeof(struct v4l2_format));

	trace_exit();
	return 0;
}


/* 尝试是否支持某种格式 */
static int up3d_try_fmt_vid_cap(struct file *file, void *fh,struct v4l2_format *f)
{
    enum v4l2_field field;
	int index=0;
	struct up3d_video_ctx *ctx = video_drvdata(file);
	
	trace_in();

	for(index=0; index<ctx->fmt_lists_cnt; index++)
	{
		if(f->fmt.pix.pixelformat == ctx->fmt_lists[index].pixel_format)
			break;
	}

	if(index >= ctx->fmt_lists_cnt)
		return -EINVAL;
	
	field = f->fmt.pix.field;
	if (field == V4L2_FIELD_ANY) {
		field = V4L2_FIELD_INTERLACED;
	} else if (V4L2_FIELD_INTERLACED != field) {
		return -EINVAL;
	}

	v4l_bound_align_image(&f->fmt.pix.width, 48, ctx->width_max, 2,	&f->fmt.pix.height, 32, ctx->height_max, 0, 0);
	f->fmt.pix.bytesperline =	f->fmt.pix.width * ctx->fmt_lists[index].bytes_per_pixel;
	f->fmt.pix.sizeimage 	=	f->fmt.pix.height * f->fmt.pix.bytesperline;

	trace_exit();
	return 0;
}

static int up3d_s_fmt_vid_cap(struct file *file, void *fh,struct v4l2_format *f)
{
	int ret;
	struct up3d_video_ctx *ctx = video_drvdata(file);

	trace_in();
	ret = up3d_try_fmt_vid_cap(file, NULL, f);
	if (ret < 0)
		return ret;

    memcpy(&ctx->cur_v4l2_format, f, sizeof(struct v4l2_format));
	
	trace_exit();
	return 0;
}

/* 枚举支持的输入设备 */
#define INPUT_DEVICE_NUMS	1
static int up3d_enum_input(struct file *file, void *fh,struct v4l2_input *inp)
{
	trace_in();
	if (inp->index >= INPUT_DEVICE_NUMS)	// 只支持一种输入设备，就是相机自身
		return -EINVAL;	
 
	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->std = V4L2_STD_525_60;
	sprintf(inp->name, "Camera %u", inp->index);
	trace_exit();
	return 0;
}

/* 获取输入设备 */
static int up3d_g_input(struct file *file, void *fh, unsigned int *i)
{
	trace_in();
	trace_exit();
	// struct vivid_dev *dev = video_drvdata(file);
	// *i = dev->input;
	return 0;
}

/* 设置输入设备 */
static int up3d_s_input(struct file *file, void *fh, unsigned int i)
{
	trace_in();
	trace_exit();
	// struct vivid_dev *dev = video_drvdata(file);
 
	// if (i >= INPUT_DEVICE_NUMS)
	// 	return -EINVAL;
 
	// dev->input = i;
	// dev->vid_cap_dev.tvnorms = V4L2_STD_ALL;
	return 0;
}


static int up3d_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	struct up3d_video_ctx *ctx = video_drvdata(file);

	trace_in();
	memcpy(cap, &ctx->cap, sizeof(struct v4l2_capability));	
	trace_exit();
	
	return 0;
}

static int up3d_enum_frameintervals(struct file *file, void *fh,
					  struct v4l2_frmivalenum *fival)
{
	trace_in();
	trace_exit();
	return 0;
}

static int up3d_enum_framesizes(struct file *file, void *fh,
				      struct v4l2_frmsizeenum *fsize)
{
	int index = 0;
	struct up3d_video_ctx *ctx = video_drvdata(file);
	
	trace_in();
	
	UP3D_DEBUG("index:%d fsize->pixel_format:0x%x type:0x%x", 
			fsize->index, fsize->pixel_format, fsize->type);

	if (fsize->index > 0)	// 目前每种格式只支持一种分辨率
	{
		UP3D_DEBUG("fsize->index:%d", fsize->index);
		return -EINVAL;
	}
		
	for(index=0; index<ctx->fmt_lists_cnt; index++)
	{
		if(fsize->pixel_format == ctx->fmt_lists[index].pixel_format)
			break;
	}

	if(index >= ctx->fmt_lists_cnt)
	{
		UP3D_DEBUG("index:%d ctx->fmt_lists_cnt:%d fsize->pixel_format:0x%x", 
			fsize->index, ctx->fmt_lists_cnt, fsize->pixel_format);
		return -EINVAL;
	}
	
	#if 1
	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;	// 这个用于区分联合体的类型 discrete、stepwise
	fsize->discrete.width = ctx->fmt_lists[index].framesize.width;
	fsize->discrete.height = ctx->fmt_lists[index].framesize.height;
	#else
	fsize->type = V4L2_FRMSIZE_TYPE_CONTINUOUS;
	fsize->stepwise.min_width = 0;
	fsize->stepwise.min_height = 0;
	fsize->stepwise.max_width = WIDTH_MAX;
	fsize->stepwise.max_height = HEIGHT_MAX;
	fsize->stepwise.step_width = fsize->stepwise.step_height = 1;
	#endif

	trace_exit();

	return 0;
}

struct v4l2_ioctl_ops up3d_v4l2_ioctl_ops =
{
    // 表示它是一个摄像头设备
    .vidioc_querycap      = up3d_querycap,

    /* 用于列举、获得、测试、设置摄像头的数据的格式 */
    .vidioc_enum_fmt_vid_cap  = up3d_enum_fmt_vid_cap,
    .vidioc_g_fmt_vid_cap     = up3d_g_fmt_vid_cap,
    .vidioc_try_fmt_vid_cap   = up3d_try_fmt_vid_cap,
    .vidioc_s_fmt_vid_cap     = up3d_s_fmt_vid_cap,
    
    /* 缓冲区操作: 申请/查询/放入队列/取出队列 使用videobuffer2提供的函数 */
	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
	// .vidioc_overlay			= vb2_ioctl_over
	// int (*vidioc_overlay)(struct file *file, void *fh, unsigned int i);
	// int (*vidioc_g_fbuf)(struct file *file, void *fh,
	// 		     struct v4l2_framebuffer *a);
	// int (*vidioc_s_fbuf)(struct file *file, void *fh,
	// 		     const struct v4l2_framebuffer *a);

	/* 输入设备相关 */
	.vidioc_enum_input		= up3d_enum_input,
	.vidioc_g_input			= up3d_g_input,
	.vidioc_s_input			= up3d_s_input,
	// .vidioc_enum_frameintervals = up3d_enum_frameintervals,		// 枚举特定格式下的帧率
	.vidioc_enum_framesizes = up3d_enum_framesizes, 			// 枚举特定格式下的
};
