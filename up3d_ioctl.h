#ifndef __UP3D_IOCTL_H__
#define __UP3D_IOCTL_H__

#include <media/v4l2-ioctl.h>
#include "up3d.h"

extern struct v4l2_ioctl_ops up3d_v4l2_ioctl_ops;
extern int up3d_init_current_format(struct up3d_video_ctx *ctx);

#endif /*__UP3D_IOCTL_H__*/