#ifndef __UP3D_VB2OPS_H__
#define __UP3D_VB2OPS_H__

#include <media/videobuf2-core.h>
#include "up3d.h"
#include <linux/irqreturn.h>

extern const struct vb2_ops up3d_vb2_ops;
extern void up3d_vb2_tasklet_handler(unsigned long data);
extern void up3d_irq_work_handler(struct work_struct *work);
extern irqreturn_t pl_cap_intc_irq_handler(int irq, void *dev_id);


extern int up3d_vb2_queue_init(struct vb2_queue *q, struct up3d_video_ctx *ctx);

#endif /*__UP3D_VB2OPS_H__*/