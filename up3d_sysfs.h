#ifndef __UP3D_SYSFS_H__
#define __UP3D_SYSFS_H__

#include <linux/fs.h>
#include <linux/slab.h>


extern const struct attribute_group up3d_attr_group;
extern const struct file_operations up3d_video_debugfs_fops;

#endif /*__UP3D_SYSFS_H__*/