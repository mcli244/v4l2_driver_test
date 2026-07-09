#ifndef __UP3D_VB2OPS_H__
#define __UP3D_VB2OPS_H__

#include <media/videobuf2-core.h>

extern const struct vb2_ops up3d_vb2_ops;
extern void up3d_vb2_tasklet_handler(unsigned long data);
extern void up3d_irq_work_handler(struct work_struct *work);


#endif /*__UP3D_VB2OPS_H__*/