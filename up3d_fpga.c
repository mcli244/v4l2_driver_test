#include "up3d_fpga.h"
#include <linux/dma-mapping.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include "up3d_vb2ops.h"


#define UP3D_FPGA_ALGO_PARAM_REG_BASE         (0x40000600)

// 算法参数寄存器
#define MIN_CONTRAST_REG 				        (0x00)
#define MEAN_OFFSET_REG 				        (0x04)
#define HIGH_LEVEL_REG 					        (0x08)
#define MIN_PEAK_REG 					        (0x0C)
#define MIN_VALUE_REG 					        (0x10)
#define JANUS_THRESHOLD_REG 			        (0x14)

// 图像地址寄存器
#define UP3D_FPGA_IMAGE_ADDR_TEST_REG           (0x18) // 32bit register 测试图的地址
#define UP3D_FPGA_IMAGE_ADDR_LIFT_REG           (0x20) // 32bit register 
#define UP3D_FPGA_IMAGE_ADDR_RIGHT_REG          (0x24) // 32bit register 
#define UP3D_FPGA_IMAGE_ADDR_RGB_REG            (0x28) // 32bit register 
#define UP3D_FPGA_ENABLE_REG                    (0x2C) // 32bit register bit0: 启动/停止FPGA传输 0: 停止 1: 启动


#define UP3D_FPGA_IOCTL_MAGIC  'V'
#define UP3D_FPGA_GET_BUF_INFO   _IOR(UP3D_FPGA_IOCTL_MAGIC, 0, struct up3d_fpga_buf_info)
#define UP3D_FPGA_SET_COMPLETED  _IOW(UP3D_FPGA_IOCTL_MAGIC, 1, __u32)
#define UP3D_FPGA_GET_ALGO_PARAMS   _IOR(UP3D_FPGA_IOCTL_MAGIC, 2, struct up3d_algo_params_t)
#define UP3D_FPGA_SET_ALGO_PARAMS   _IOW(UP3D_FPGA_IOCTL_MAGIC, 3, struct up3d_algo_params_t)

struct up3d_fpga_buf_info {
	__u64 phys_addr;      /* CPU 物理地址，如 0x8a100000 */
	__u32 size;           /* buffer 总大小 */
	__u32 frame_size;     /* 单帧大小，如 832*608 */
};

struct up3d_algo_params_t {
	int min_contrast;      /* 最小对比度 */
	int mean_offset;       /* 均值偏移 */
	int high_level;        /* 高电平阈值 */
	int min_peak;          /* 最小峰值 */
	int min_value;         /* 最小值 */
	int janus_threshold;   /* janus 阈值 */
};


static void *input_image_cpu_addr = NULL;
static dma_addr_t input_image_dma_addr = 0;

int up3d_fpga_set_test_image_addr(struct up3d_video_ctx *ctx, u32 addr)
{
    if (ctx == NULL){
		dev_err(ctx->dev, "ctx is NULL\n");
		return -EINVAL;
	}
	dev_dbg(ctx->dev, "test_image_addr: 0x%x\n", addr);
	writel(addr, ctx->fpga_base_addr + UP3D_FPGA_IMAGE_ADDR_TEST_REG);
	return 0;
}

int up3d_fpga_set_output_image_addr(struct up3d_video_ctx *ctx, u32 addr)
{
    if (ctx == NULL){
		dev_err(ctx->dev, "ctx is NULL\n");
		return -EINVAL;
	}

    u32 left_addr = addr;
    u32 right_addr = addr + ctx->output_images[1].width * ctx->output_images[1].height * ctx->output_images[1].bytes_per_pixel;
    u32 rgb_addr = right_addr + ctx->output_images[2].width * ctx->output_images[2].height * ctx->output_images[2].bytes_per_pixel;

    // TODO: 注意4字节对齐
	dev_dbg(ctx->dev, "left_addr: 0x%x, right_addr: 0x%x, rgb_addr: 0x%x\n", left_addr, right_addr, rgb_addr);
    writel(left_addr, ctx->fpga_base_addr + UP3D_FPGA_IMAGE_ADDR_LIFT_REG);
    writel(right_addr, ctx->fpga_base_addr + UP3D_FPGA_IMAGE_ADDR_RIGHT_REG);
    writel(rgb_addr, ctx->fpga_base_addr + UP3D_FPGA_IMAGE_ADDR_RGB_REG);

    return 0;
}

int up3d_fpga_ctrl(struct up3d_video_ctx *ctx, int enable)
{
    if (ctx == NULL){
		dev_err(ctx->dev, "ctx is NULL\n");
		return -EINVAL;
	}

	int v = readl(ctx->fpga_base_addr + UP3D_FPGA_ENABLE_REG);
	if(enable)
		v |= (0x01 << 0);
	else
	 	v &= ~(0x01 << 0);
    writel(v, ctx->fpga_base_addr + UP3D_FPGA_ENABLE_REG);
    return 0;
}

int up3d_fpga_irq_control(struct up3d_video_ctx *ctx, int control_irq)
{
    if (ctx == NULL){
		dev_err(ctx->dev, "ctx is NULL\n");
		return -EINVAL;
	}

	int v = readl(ctx->fpga_base_addr + UP3D_FPGA_ENABLE_REG);
	if(control_irq)
		v |= (0x01 << 1);
	else
		v &= ~(0x01 << 1);
    writel(v, ctx->fpga_base_addr + UP3D_FPGA_ENABLE_REG);
    return 0;
}

int up3d_fpga_irq_clear(struct up3d_video_ctx *ctx)
{
    if (ctx == NULL){
		dev_err(ctx->dev, "ctx is NULL\n");
		return -EINVAL;
	}

	int v = readl(ctx->fpga_base_addr + UP3D_FPGA_ENABLE_REG);
	v |= (0x01 << 1);
    writel(v, ctx->fpga_base_addr + UP3D_FPGA_ENABLE_REG);
    return 0;
}

static struct up3d_video_ctx *file_to_up3d_fpga_ctx(struct file *file)
{
	return container_of(file->private_data, struct up3d_video_ctx, fpga_miscdev);
}

static int up3d_fpga_open(struct inode *inode, struct file *file)
{
	struct up3d_video_ctx *ctx = file_to_up3d_fpga_ctx(file);
	if (!ctx)
		return -ENODEV;

	return 0;
}

static int up3d_fpga_release(struct inode *inode, struct file *file)
{
	return 0;
}

static int up3d_fpga_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct up3d_video_ctx *ctx = file_to_up3d_fpga_ctx(file);
	unsigned long size;

	if (!input_image_cpu_addr)
	{
		dev_err(ctx->dev, "input_image_cpu_addr is NULL\n");
		return -EINVAL;
	}

	// size = ctx->input_image.width * ctx->input_image.height * ctx->input_image.bytes_per_pixel;
	size = ctx->input_image_buffer_size;
	if (vma->vm_pgoff != 0){
		dev_err(ctx->dev, "vma_pgoff is not 0\n");	
		return -EINVAL;
	}

	if ((vma->vm_end - vma->vm_start) > size){
		dev_err(ctx->dev, "input_image size is too large, vma_end: %lu, vma_start: %lu, size: %lu\n", vma->vm_end, vma->vm_start, size);
		return -EINVAL;
	}

	return dma_mmap_coherent(ctx->dev,
							vma,
							input_image_cpu_addr,
							input_image_dma_addr,
							size);
}


static void get_algo_params(struct up3d_video_ctx *ctx, struct up3d_algo_params_t *algo_params)
{
	algo_params->min_contrast = readl_relaxed(ctx->fpga_base_addr + MIN_CONTRAST_REG);
	algo_params->mean_offset = readl_relaxed(ctx->fpga_base_addr + MEAN_OFFSET_REG);
	algo_params->high_level = readl_relaxed(ctx->fpga_base_addr + HIGH_LEVEL_REG);
	algo_params->min_peak = readl_relaxed(ctx->fpga_base_addr + MIN_PEAK_REG);
	algo_params->min_value = readl_relaxed(ctx->fpga_base_addr + MIN_VALUE_REG);
	algo_params->janus_threshold = readl_relaxed(ctx->fpga_base_addr + JANUS_THRESHOLD_REG);
}

static void set_algo_params(struct up3d_video_ctx *ctx, struct up3d_algo_params_t *algo_params)
{
	writel_relaxed(algo_params->min_contrast, ctx->fpga_base_addr + MIN_CONTRAST_REG);
	writel_relaxed(algo_params->mean_offset, ctx->fpga_base_addr + MEAN_OFFSET_REG);
	writel_relaxed(algo_params->high_level, ctx->fpga_base_addr + HIGH_LEVEL_REG);
	writel_relaxed(algo_params->min_peak, ctx->fpga_base_addr + MIN_PEAK_REG);
	writel_relaxed(algo_params->min_value, ctx->fpga_base_addr + MIN_VALUE_REG);
	writel_relaxed(algo_params->janus_threshold, ctx->fpga_base_addr + JANUS_THRESHOLD_REG);
}

static void show_buf_info(struct up3d_video_ctx *ctx)
{
	dev_info(ctx->dev, "algo_base_addr: 0x%pK -- 0x%x\n", 
		ctx->fpga_base_addr, UP3D_FPGA_ALGO_PARAM_REG_BASE);
	dev_info(ctx->dev, "min_contrast: 0x%x(%d)\n", 
		readl_relaxed(ctx->fpga_base_addr + MIN_CONTRAST_REG), 
		readl_relaxed(ctx->fpga_base_addr + MIN_CONTRAST_REG));
	dev_info(ctx->dev, "mean_offset: 0x%x(%d)\n", 
		readl_relaxed(ctx->fpga_base_addr + MEAN_OFFSET_REG), 
		readl_relaxed(ctx->fpga_base_addr + MEAN_OFFSET_REG));
	dev_info(ctx->dev, "high_level: 0x%x(%d)\n", 
		readl_relaxed(ctx->fpga_base_addr + HIGH_LEVEL_REG), 
		readl_relaxed(ctx->fpga_base_addr + HIGH_LEVEL_REG));
	dev_info(ctx->dev, "min_peak: 0x%x(%d)\n", 
		readl_relaxed(ctx->fpga_base_addr + MIN_PEAK_REG), 
		readl_relaxed(ctx->fpga_base_addr + MIN_PEAK_REG));
	dev_info(ctx->dev, "min_value: 0x%x(%d)\n", 
		readl_relaxed(ctx->fpga_base_addr + MIN_VALUE_REG), 
		readl_relaxed(ctx->fpga_base_addr + MIN_VALUE_REG));
	dev_info(ctx->dev, "janus_threshold: 0x%x(%d)\n", 
		readl_relaxed(ctx->fpga_base_addr + JANUS_THRESHOLD_REG), 
		readl_relaxed(ctx->fpga_base_addr + JANUS_THRESHOLD_REG));
}


static long up3d_fpga_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct up3d_fpga_buf_info buf_info;
	struct up3d_algo_params_t algo_params;
	__u32 val;

	struct up3d_video_ctx *ctx = file_to_up3d_fpga_ctx(file);
	if (!ctx)
		return -ENODEV;

	switch (cmd) {
	case UP3D_FPGA_GET_BUF_INFO:
		buf_info.phys_addr = input_image_dma_addr;
		buf_info.size = ctx->input_image.width * ctx->input_image.height * ctx->input_image.bytes_per_pixel;
		buf_info.frame_size = ctx->input_image.width * ctx->input_image.height * ctx->input_image.bytes_per_pixel;
		if (copy_to_user((void __user *)arg, &buf_info, sizeof(buf_info)))
			return -EFAULT;
		break;
	case UP3D_FPGA_SET_COMPLETED:
		if (copy_from_user(&val, (void __user *)arg, sizeof(val)))
			return -EFAULT;
		gpiod_set_value_cansleep(ctx->completed_gpio, val ? 1 : 0);
		break;
	case UP3D_FPGA_GET_ALGO_PARAMS:
		get_algo_params(ctx, &algo_params);
		if (copy_to_user((void __user *)arg, &algo_params, sizeof(algo_params)))
			return -EFAULT;
		show_buf_info(ctx);
		break;
	case UP3D_FPGA_SET_ALGO_PARAMS:
		if (copy_from_user(&algo_params, (void __user *)arg, sizeof(algo_params)))
			return -EFAULT;
		set_algo_params(ctx, &algo_params);
		show_buf_info(ctx);
		break;
	default:
		return -ENOTTY;
	}

	return 0;
}


static const struct file_operations up3d_fpga_fops = {
	.owner = THIS_MODULE,
	.open = up3d_fpga_open,
	.release = up3d_fpga_release,
	.mmap = up3d_fpga_mmap,
	.unlocked_ioctl = up3d_fpga_ioctl,
};


static void up3d_init_input_output_images_address(struct up3d_video_ctx *ctx)
{
	ctx->input_image.width = 832;
	ctx->input_image.height = 608;
	ctx->input_image.bytes_per_pixel = 1;

	ctx->output_images[0].width = 832;
	ctx->output_images[0].height = 608;
	ctx->output_images[0].bytes_per_pixel = 1;

	ctx->output_images[1].width = 832;
	ctx->output_images[1].height = 608;
	ctx->output_images[1].bytes_per_pixel = 1;

	ctx->output_images[2].width = 832;
	ctx->output_images[2].height = 608;
	ctx->output_images[2].bytes_per_pixel = 1;
}

int up3d_fpga_init(struct up3d_video_ctx *ctx)
{
	int ret = 0;
	if(!ctx || !ctx->dev)
		return -EINVAL;

	up3d_init_input_output_images_address(ctx);

	if(ctx->input_image.width > WIDTH_MAX || ctx->input_image.height > HEIGHT_MAX)
	{
		dev_err(ctx->dev, "input_image width or height is too large\n");
		return -EINVAL;
	}

	ctx->input_image_buffer_size = 2*1024*1024;	// 固定给2MB
	input_image_cpu_addr = dma_alloc_coherent(ctx->dev,
								ctx->input_image_buffer_size,
								&input_image_dma_addr,
								GFP_KERNEL);
	if (!input_image_cpu_addr){	
		dev_err(ctx->dev, "Failed to allocate memory for input image\n");
		return -ENOMEM;
	}

	dev_dbg(ctx->dev, "input_image_cpu_addr=%pK input_image_dma_addr=%pad\n", input_image_cpu_addr, &input_image_dma_addr);

	ctx->fpga_base_addr = ioremap(UP3D_FPGA_ALGO_PARAM_REG_BASE, 512);
	if (!ctx->fpga_base_addr) {
		dev_err(ctx->dev, "Failed to remap algo base address\n");
		ret = -ENOMEM;
		goto dma_free;
	}

	ctx->completed_gpio = devm_gpiod_get_optional(ctx->dev, "completed", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->completed_gpio)) {
		dev_err(ctx->dev, "Failed to get completed_gpio\n");
		ret = -ENOMEM;
		goto umap_free;
	}
	gpiod_set_value_cansleep(ctx->completed_gpio, 0);

	ctx->irq = platform_get_irq(ctx->pdev, 0);
	if (ctx->irq < 0){
		dev_err(ctx->dev, "Failed to get IRQ\n");
		ret = -EINVAL;
		goto umap_free;
	}
	INIT_WORK(&ctx->irq_work, up3d_irq_work_handler);

	up3d_fpga_irq_clear(ctx);
	ctx->device_info.fpga_discarded_frames_cnt = 0;
	if (devm_request_irq(ctx->dev, ctx->irq, pl_cap_intc_irq_handler, 
		IRQF_TRIGGER_RISING, "pl_cap_intc", ctx))
	{
		dev_err(ctx->dev, "Failed to request IRQ\n");
		ret = -EINVAL;
		goto umap_free;
	}

	up3d_fpga_set_test_image_addr(ctx, (u32)input_image_dma_addr);

	ctx->fpga_miscdev.name = "up3d-fpga";
	ctx->fpga_miscdev.minor = MISC_DYNAMIC_MINOR;
	ctx->fpga_miscdev.fops = &up3d_fpga_fops;
	ctx->fpga_miscdev.parent = ctx->dev;
	ret = misc_register(&ctx->fpga_miscdev);
	if (ret < 0) {
		dev_err(ctx->dev, "Failed to register misc device\n");
		goto umap_free;
	}

	dev_dbg(ctx->dev, "up3d_fpga probe success\n");

    return 0;

umap_free:
	iounmap(ctx->fpga_base_addr);
	ctx->fpga_base_addr = NULL;

dma_free:
	dma_free_coherent(ctx->dev,
		ctx->input_image.width * ctx->input_image.height * ctx->input_image.bytes_per_pixel,
		input_image_cpu_addr,
		input_image_dma_addr);
	input_image_cpu_addr = NULL;
	input_image_dma_addr = 0;
    return ret;
}

int up3d_fpga_exit(struct up3d_video_ctx *ctx)
{
	if(!ctx || !ctx->dev)
		return -EINVAL;

	disable_irq(ctx->irq);

	cancel_work_sync(&ctx->irq_work);

    if (ctx->fpga_miscdev.this_device) {
        misc_deregister(&ctx->fpga_miscdev);
        ctx->fpga_miscdev.this_device = NULL;
    }

    if (ctx->fpga_base_addr) {
        iounmap(ctx->fpga_base_addr);
        ctx->fpga_base_addr = NULL;
    }

    if (input_image_cpu_addr) {
        dma_free_coherent(ctx->dev,
            ctx->input_image.width * ctx->input_image.height * ctx->input_image.bytes_per_pixel,
            input_image_cpu_addr,
            input_image_dma_addr);
        input_image_cpu_addr = NULL;
        input_image_dma_addr = 0;
    }

    return 0;
}
