#include "up3d_ioctl.h"
#include "up3d.h"

#define INPUT_DEVICE_NUMS 1

static const struct v4l2_frmsize_discrete grey_sizes[] = {
	{832, 608*3},
};

/* 列举支持哪种格式 */
static int up3d_enum_fmt_vid_cap(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	struct up3d_video_ctx *ctx = video_drvdata(file);

	if (f->index >= ctx->fmt_lists_cnt)
		return -EINVAL;
	strcpy(f->description, ctx->fmt_lists[f->index].description);
	f->pixelformat = ctx->fmt_lists[f->index].pixel_format;

	return 0;
}

/* 获取当前使用的格式 */
static int up3d_g_fmt_vid_cap(struct file *file, void *fh, struct v4l2_format *f)
{
	struct up3d_video_ctx *ctx = video_drvdata(file);
	memcpy(f, &ctx->cur_v4l2_format, sizeof(struct v4l2_format));
	return 0;
}

/* 尝试是否支持某种格式 */
static int up3d_try_fmt_vid_cap(struct file *file, void *fh, struct v4l2_format *f)
{
	enum v4l2_field field;
	int index = 0;
	struct up3d_video_ctx *ctx = video_drvdata(file);

	for (index = 0; index < ctx->fmt_lists_cnt; index++)
	{
		if (f->fmt.pix.pixelformat == ctx->fmt_lists[index].pixel_format)
			break;
	}

	if (index >= ctx->fmt_lists_cnt){
		dev_err(ctx->dev, "index:%d ctx->fmt_lists_cnt:%d f->fmt.pix.pixelformat:0x%x",
				index, ctx->fmt_lists_cnt, f->fmt.pix.pixelformat);
		return -EINVAL;
	}

	field = f->fmt.pix.field;
	if (field == V4L2_FIELD_ANY)
	{
		dev_err(ctx->dev, "field:0x%x", field);
		field = V4L2_FIELD_INTERLACED;
	}
	else if (V4L2_FIELD_INTERLACED != field)
	{
		dev_err(ctx->dev, "field:0x%x", field);
		return -EINVAL;
	}

	v4l_bound_align_image(&f->fmt.pix.width, 48, ctx->width_max, 2, &f->fmt.pix.height, 32, ctx->height_max, 0, 0);
	f->fmt.pix.bytesperline = f->fmt.pix.width * ctx->fmt_lists[index].bytes_per_pixel;
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;

	return 0;
}

static int up3d_s_fmt_vid_cap(struct file *file, void *fh, struct v4l2_format *f)
{
	int ret;
	struct up3d_video_ctx *ctx = video_drvdata(file);

	ret = up3d_try_fmt_vid_cap(file, NULL, f);
	if (ret < 0)
		return ret;

	memcpy(&ctx->cur_v4l2_format, f, sizeof(struct v4l2_format));

	return 0;
}

static int up3d_enum_input(struct file *file, void *fh, struct v4l2_input *inp)
{
	if (inp->index >= INPUT_DEVICE_NUMS) // 只支持一种输入设备，就是相机自身
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->std = V4L2_STD_525_60;
	sprintf(inp->name, "Camera %u", inp->index);

	return 0;
}

static int up3d_g_input(struct file *file, void *fh, unsigned int *i)
{
	return 0;
}

static int up3d_s_input(struct file *file, void *fh, unsigned int i)
{
	return 0;
}

static int up3d_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	struct up3d_video_ctx *ctx = video_drvdata(file);
	memcpy(cap, &ctx->cap, sizeof(struct v4l2_capability));
	return 0;
}

static int up3d_enum_framesizes(struct file *file, void *fh,
								struct v4l2_frmsizeenum *fsize)
{
	int index = 0;
	struct up3d_video_ctx *ctx = video_drvdata(file);

	dev_dbg(ctx->dev, "index:%d fsize->pixel_format:0x%x type:0x%x",
			fsize->index, fsize->pixel_format, fsize->type);

	for (index = 0; index < ctx->fmt_lists_cnt; index++)
	{
		if (fsize->pixel_format == ctx->fmt_lists[index].pixel_format)
			break;
	}

	if (index >= ctx->fmt_lists_cnt)
	{
		dev_err(ctx->dev, "index:%d ctx->fmt_lists_cnt:%d fsize->pixel_format:0x%x",
				fsize->index, ctx->fmt_lists_cnt, fsize->pixel_format);
		return -EINVAL;
	}

	switch (ctx->fmt_lists[index].pixel_format)
	{
	case V4L2_PIX_FMT_GREY:
		if (fsize->index >= ARRAY_SIZE(grey_sizes))
			return -EINVAL;
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete = grey_sizes[fsize->index];
		break;
	default:
		return -EINVAL;
		break;
	}

	return 0;
}

static int up3d_g_ctrl(struct file *file, void *fh,
					   struct v4l2_control *a)
{
	struct up3d_video_ctx *ctx = video_drvdata(file);

	switch (a->id)
	{
	case V4L2_CID_BRIGHTNESS:
		a->value = ctx->input_brightness;
		dev_info(ctx->dev, "up3d_g_ctrl V4L2_CID_BRIGHTNESS ctrl->val:%d\n", a->value);
		break;
	}

	return 0;
}

static int up3d_s_ctrl(struct file *file, void *fh,
					   struct v4l2_control *a)
{
	struct up3d_video_ctx *ctx = video_drvdata(file);

	switch (a->id)
	{
	case V4L2_CID_BRIGHTNESS:
		ctx->input_brightness = a->value;
		dev_info(ctx->dev, "up3d_s_ctrl V4L2_CID_BRIGHTNESS ctrl->val:%d\n", a->value);
		break;
	}

	return 0;
}

static long up3d_vidioc_default(struct file *file, void *priv,
			       bool valid_prio, unsigned int cmd, void *param)
{
	struct up3d_device_info *status = (struct up3d_device_info *)param;
	struct up3d_video_ctx *ctx = video_drvdata(file);

    if (cmd == VIDIOC_UP3D_GET_STATUS) {
		memcpy(&ctx->device_info.cur_v4l2_format, &ctx->cur_v4l2_format, sizeof(struct v4l2_format));
		memcpy(status, &ctx->device_info, sizeof(struct up3d_device_info));
        return 0;
    }

    return -ENOTTY; // Not a valid ioctl
}



struct v4l2_ioctl_ops up3d_v4l2_ioctl_ops =
	{
		.vidioc_querycap = up3d_querycap,
		.vidioc_enum_fmt_vid_cap = up3d_enum_fmt_vid_cap,
		.vidioc_g_fmt_vid_cap = up3d_g_fmt_vid_cap,
		.vidioc_try_fmt_vid_cap = up3d_try_fmt_vid_cap,
		.vidioc_s_fmt_vid_cap = up3d_s_fmt_vid_cap,
		.vidioc_reqbufs = vb2_ioctl_reqbufs,
		.vidioc_create_bufs = vb2_ioctl_create_bufs,
		.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
		.vidioc_querybuf = vb2_ioctl_querybuf,
		.vidioc_qbuf = vb2_ioctl_qbuf,
		.vidioc_dqbuf = vb2_ioctl_dqbuf,
		.vidioc_expbuf = vb2_ioctl_expbuf,
		.vidioc_streamon = vb2_ioctl_streamon,
		.vidioc_streamoff = vb2_ioctl_streamoff,
		.vidioc_enum_input = up3d_enum_input,
		.vidioc_g_input = up3d_g_input,
		.vidioc_s_input = up3d_s_input,
		.vidioc_enum_framesizes = up3d_enum_framesizes,
		.vidioc_g_ctrl = up3d_g_ctrl,
		.vidioc_s_ctrl = up3d_s_ctrl,

		.vidioc_default       = up3d_vidioc_default
};
