/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM blk

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_BLK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_BLK_H

#include <trace/hooks/vendor_hooks.h>

struct block_device;
struct gendisk;

DECLARE_HOOK(android_vh_bd_link_disk_holder,
	TP_PROTO(struct block_device *bdev, struct gendisk *disk),
	TP_ARGS(bdev, disk));

struct path;
struct vfsmount;

DECLARE_HOOK(android_vh_do_new_mount_fc,
	TP_PROTO(struct path *mountpoint, struct vfsmount *mnt),
	TP_ARGS(mountpoint, mnt));
<<<<<<< HEAD   (ff78c78d65d3b2e46c6cd4c1b59e8e0e6150cdae ANDROID: GKI: update symbol list for allwinner)

struct blk_mq_hw_ctx;
struct request_queue;

||||||| BASE   (eed112409dee72c96e471760d8283476fe8111e2 Revert "ANDROID: block: Introduce the android_rvh_blk_mq_has)
DECLARE_HOOK(android_vh_f2fs_ra_op_flags,
	TP_PROTO(blk_opf_t *op_flag, struct readahead_control *rac),
	TP_ARGS(op_flag, rac));
=======
DECLARE_RESTRICTED_HOOK(android_rvh_blk_mq_has_request,
	TP_PROTO(struct blk_mq_hw_ctx *hctx, struct request *rq, bool *ret),
	TP_ARGS(hctx, rq, ret), 1);
DECLARE_HOOK(android_vh_f2fs_ra_op_flags,
	TP_PROTO(blk_opf_t *op_flag, struct readahead_control *rac),
	TP_ARGS(op_flag, rac));
>>>>>>> CHANGE (e4482a882fb0b4044fef96e02b1cf32b4e370946 ANDROID: block: Introduce the android_rvh_blk_mq_has_request)
DECLARE_HOOK(android_vh_blk_mq_delay_run_hw_queue,
	TP_PROTO(int cpu, struct blk_mq_hw_ctx *hctx, unsigned long delay, bool *skip),
	TP_ARGS(cpu, hctx, delay, skip));

DECLARE_HOOK(android_vh_blk_mq_kick_requeue_list,
	TP_PROTO(struct request_queue *q, unsigned long delay, bool *skip),
	TP_ARGS(q, delay, skip));

struct readahead_control;
typedef __u32 __bitwise blk_opf_t;

DECLARE_HOOK(android_vh_f2fs_ra_op_flags,
	TP_PROTO(blk_opf_t *op_flag, struct readahead_control *rac),
	TP_ARGS(op_flag, rac));

#endif /* _TRACE_HOOK_BLK_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
