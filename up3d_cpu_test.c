#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <linux/mm.h>
#include <linux/ioctl.h>
#include <linux/types.h>

#include "up3d_cpu_test.h"

// CPU fill image to DDR
// input Image --> CPU --> DDR --> FPGA --> DDR --> CPU --> OUT image
// app: /dev/up3d-up800w-test


static struct up3d_cpu_test_dev_t up3d_cpu_test_dev;

static struct up3d_cpu_test_dev_t *file_to_up3d_test_dev(struct file *file)
{
	return container_of(file->private_data, struct up3d_cpu_test_dev_t, miscdev);
}

static int up3d_cpu_test_open(struct inode *inode, struct file *file)
{
	struct up3d_cpu_test_dev_t *up3d_cpu_test_dev = file_to_up3d_test_dev(file);
	if (!up3d_cpu_test_dev)
		return -ENODEV;

	return 0;
}

static int up3d_cpu_test_release(struct inode *inode, struct file *file)
{
	return 0;
}

static int up3d_cpu_test_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long pfn;
	struct up3d_cpu_test_dev_t *up3d_cpu_test_dev = file_to_up3d_test_dev(file);

	if (!up3d_cpu_test_dev)
		return -ENODEV;

	if (offset + size > up3d_cpu_test_dev->buf_info.size)
		return -EINVAL;

	pfn = (up3d_cpu_test_dev->buf_info.phys_addr + offset) >> PAGE_SHIFT;
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	return remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot);
}

static long up3d_cpu_test_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct up3d_camera_buf_info buf_info;
	__u32 val;

	struct up3d_cpu_test_dev_t *up3d_cpu_test_dev = file_to_up3d_test_dev(file);
	if (!up3d_cpu_test_dev)
		return -ENODEV;

	switch (cmd) {
	case UP3D_CAMERA_GET_BUF_INFO:
		buf_info.phys_addr = up3d_cpu_test_dev->buf_info.phys_addr;
		buf_info.size = up3d_cpu_test_dev->buf_info.size;
		buf_info.frame_size = up3d_cpu_test_dev->buf_info.frame_size;
		if (copy_to_user((void __user *)arg, &buf_info, sizeof(buf_info)))
			return -EFAULT;
		break;
	case UP3D_CAMERA_SET_COMPLETED:
		if (copy_from_user(&val, (void __user *)arg, sizeof(val)))
			return -EFAULT;
		gpiod_set_value_cansleep(up3d_cpu_test_dev->completed_gpio, val ? 1 : 0);
		break;
	default:
		return -ENOTTY;
	}

	return 0;
}

static const struct file_operations up3d_cpu_test_fops = {
	.owner = THIS_MODULE,
	.open = up3d_cpu_test_open,
	.release = up3d_cpu_test_release,
	.mmap = up3d_cpu_test_mmap,
	.unlocked_ioctl = up3d_cpu_test_ioctl,
};

int up3d_cpu_test_init(struct platform_device *pdev)
{
    struct resource res;
	int ret = 0;
    struct up3d_cpu_test_dev_t *up3d_cpu_test = &up3d_cpu_test_dev;
    if (!up3d_cpu_test) {
        dev_err(&pdev->dev, "Failed to allocate memory\n");
        return -ENOMEM;
    }

	// up3d_cpu_test->dev = &pdev->dev;
	// dev_set_drvdata(&pdev->dev, up3d_cpu_test);

    struct device_node *np = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	if (!np){
		dev_err(&pdev->dev, "Failed to parse memory-region\n");
		ret = -ENOMEM;
		goto free_up3d_cpu_test;
	}

	if (of_address_to_resource(np, 0, &res)) {
		dev_err(&pdev->dev, "Failed to get reserved memory resource\n");
		of_node_put(np);
		ret = -ENOMEM;
		goto free_up3d_cpu_test;
	}
	up3d_cpu_test->buf_info.phys_addr = res.start;
	up3d_cpu_test->buf_info.size = resource_size(&res);
	up3d_cpu_test->buf_info.frame_size = 832 * 608;

	up3d_cpu_test->completed_gpio = devm_gpiod_get_optional(&pdev->dev, "completed", GPIOD_OUT_LOW);
	if (IS_ERR(up3d_cpu_test->completed_gpio)) {
		dev_err(&pdev->dev, "Failed to get completed_gpio\n");
		ret = -ENOMEM;
		goto free_up3d_cpu_test;
	}
	gpiod_set_value_cansleep(up3d_cpu_test->completed_gpio, 0);

	up3d_cpu_test->miscdev.name = "up3d-up800w-test";
	up3d_cpu_test->miscdev.minor = MISC_DYNAMIC_MINOR;
	up3d_cpu_test->miscdev.fops = &up3d_cpu_test_fops;
	up3d_cpu_test->miscdev.parent = &pdev->dev;
	ret = misc_register(&up3d_cpu_test->miscdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register misc device\n");
		goto free_up3d_cpu_test;
	}

	dev_info(&pdev->dev, "up3d_cpu_test probe success\n");

    return 0;

free_up3d_cpu_test:
//     devm_kfree(&pdev->dev, up3d_cpu_test);
    return ret;
}

void up3d_cpu_test_exit(struct platform_device *pdev)
{
    struct up3d_cpu_test_dev_t *up3d_cpu_test = &up3d_cpu_test_dev;
	if (!up3d_cpu_test)
		return;
	misc_deregister(&up3d_cpu_test->miscdev);
	gpiod_set_value_cansleep(up3d_cpu_test->completed_gpio, 0);
	// devm_kfree(&pdev->dev, up3d_cpu_test);
}
