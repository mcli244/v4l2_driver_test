#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/slab.h>


#include "up3d_sysfs.h"
#include "up3d.h"


/*
struct up3d_device_info {
    uint8_t 	status;
	uint8_t 	irq_is_disable;
	uint32_t 	irq_count;
	uint16_t 	vb_total;
	uint16_t 	vb_free;
	uint16_t 	vb_free_min;
	uint16_t 	vb_queue_overflow;
	struct v4l2_format 		cur_v4l2_format;
};
*/


static ssize_t generic_show(struct device *dev,
                            struct device_attribute *attr, char *buf, 
                            const char *field_name)
{
    struct up3d_video_ctx *ctx = platform_get_drvdata(to_platform_device(dev));

    if (ctx == NULL)
        return -EINVAL;

    if (strcmp(field_name, "status") == 0)
        return sprintf(buf, "%s\n", ctx->device_info.status ? "RUN" : "STOP");
    else if (strcmp(field_name, "irq_is_disable") == 0)
        return sprintf(buf, "%s\n", ctx->device_info.irq_is_disable ? "DISABLE" : "ENABLE");
    else if (strcmp(field_name, "irq_count") == 0)
        return sprintf(buf, "%d\n", ctx->device_info.irq_count);
    else if (strcmp(field_name, "vb_total") == 0)
        return sprintf(buf, "%d\n", ctx->device_info.vb_total);
    else if (strcmp(field_name, "vb_free") == 0)
        return sprintf(buf, "%d\n", ctx->device_info.vb_free);
    else if (strcmp(field_name, "vb_free_min") == 0)
        return sprintf(buf, "%d\n", ctx->device_info.vb_free_min);
    else if (strcmp(field_name, "vb_queue_overflow") == 0)
        return sprintf(buf, "%d\n", ctx->device_info.vb_queue_overflow);
    else if (strcmp(field_name, "cur_v4l2_format") == 0)
        return sprintf(buf, "0x%x %d x %d\n", ctx->device_info.cur_v4l2_format.fmt.pix.pixelformat,
                       ctx->device_info.cur_v4l2_format.fmt.pix.width,
                       ctx->device_info.cur_v4l2_format.fmt.pix.height);

    return -EINVAL;
}

#define DEVICE_ATTR_GENERIC(name, field)                      \
static ssize_t name##_show(struct device *dev,                 \
                           struct device_attribute *attr,      \
                           char *buf)                         \
{                                                             \
    return generic_show(dev, attr, buf, field);               \
}                                                             \
static DEVICE_ATTR_RO(name);

DEVICE_ATTR_GENERIC(status, "status");
DEVICE_ATTR_GENERIC(irq_is_disable, "irq_is_disable");
DEVICE_ATTR_GENERIC(irq_count, "irq_count");
DEVICE_ATTR_GENERIC(vb_total, "vb_total");
DEVICE_ATTR_GENERIC(vb_free, "vb_free");
DEVICE_ATTR_GENERIC(vb_free_min, "vb_free_min");
DEVICE_ATTR_GENERIC(vb_queue_overflow, "vb_queue_overflow");
DEVICE_ATTR_GENERIC(cur_v4l2_format, "cur_v4l2_format");


static struct attribute *up3d_attrs[] = {
    &dev_attr_status.attr,
    &dev_attr_irq_is_disable.attr,
    &dev_attr_irq_count.attr,
    &dev_attr_vb_total.attr,
    &dev_attr_vb_free.attr,
    &dev_attr_vb_free_min.attr,
    &dev_attr_vb_queue_overflow.attr,
    &dev_attr_cur_v4l2_format.attr,
    NULL,
};

const struct attribute_group up3d_attr_group = {
    .attrs = up3d_attrs,
};


static int up3d_video_debugfs_open(struct inode *inode, struct file *file)
{
    file->private_data = inode->i_private;
    return 0;
}

static ssize_t up3d_video_debugfs_read(struct file *file, char __user *buf,
                              size_t count, loff_t *ppos)
{
	char tmp[256];
	int len = 0;
    struct up3d_video_ctx *ctx = file->private_data;
	if (!ctx)
		return -EINVAL;

    len = snprintf(tmp, sizeof(tmp), "status:%d  irq_count:%d vb_total:%d vb_free:%d vb_free_min:%d vb_queue_overflow:%d \n"
		"pixelformat:0x%x %d x %d\n", 
		ctx->device_info.status, 
		ctx->device_info.irq_count,
		ctx->device_info.vb_total,
		ctx->device_info.vb_free,
		ctx->device_info.vb_free_min,
		ctx->device_info.vb_queue_overflow,
		ctx->device_info.cur_v4l2_format.fmt.pix.pixelformat,
		ctx->device_info.cur_v4l2_format.fmt.pix.width,
		ctx->device_info.cur_v4l2_format.fmt.pix.height);
    return simple_read_from_buffer(buf, count, ppos, tmp, len);
}

const struct file_operations up3d_video_debugfs_fops = {
    .owner = THIS_MODULE,
    .read = up3d_video_debugfs_read,
	.open = up3d_video_debugfs_open,
	.llseek = default_llseek,
};