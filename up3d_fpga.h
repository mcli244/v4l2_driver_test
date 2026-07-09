#ifndef __UP3D_FPGA_H__
#define __UP3D_FPGA_H__

#include <linux/types.h>
#include "up3d.h"

int up3d_fpga_init(struct up3d_video_ctx *ctx);
int up3d_fpga_exit(struct up3d_video_ctx *ctx);
int up3d_fpga_set_test_image_addr(struct up3d_video_ctx *ctx, u32 addr);
int up3d_fpga_set_output_image_addr(struct up3d_video_ctx *ctx, u32 addr);
int up3d_fpga_ctrl(struct up3d_video_ctx *ctx, int enable);
int up3d_fpga_irq_clear(struct up3d_video_ctx *ctx);   
int up3d_fpga_irq_control(struct up3d_video_ctx *ctx, int control_irq);

#endif