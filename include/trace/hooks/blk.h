<<<<<<< HEAD   (029c3b29942fa7c9965736a5ab9030f41c9c02d3 ANDROID: ABI: New variables and hooks added, honor symbol li)
||||||| BASE   (eafcd29b88b645eaef132d334fd1ffe290c10435 ANDROID: GKI: db845c: Add _totalram_pages to symbol list)
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
DECLARE_HOOK(android_vh_blk_fill_rwbs,
	TP_PROTO(char *rwbs, unsigned int opf),
	TP_ARGS(rwbs, opf));

struct path;
struct vfsmount;

DECLARE_HOOK(android_vh_do_new_mount_fc,
	TP_PROTO(struct path *mountpoint, struct vfsmount *mnt),
	TP_ARGS(mountpoint, mnt));

struct readahead_control;
typedef __u32 __bitwise blk_opf_t;

DECLARE_HOOK(android_vh_f2fs_ra_op_flags,
	TP_PROTO(blk_opf_t *op_flag, struct readahead_control *rac),
	TP_ARGS(op_flag, rac));

struct blk_mq_hw_ctx;
struct request_queue;

DECLARE_HOOK(android_vh_blk_mq_delay_run_hw_queue,
	TP_PROTO(int cpu, struct blk_mq_hw_ctx *hctx, unsigned long delay, bool *skip),
	TP_ARGS(cpu, hctx, delay, skip));

DECLARE_HOOK(android_vh_blk_mq_kick_requeue_list,
	TP_PROTO(struct request_queue *q, unsigned long delay, bool *skip),
	TP_ARGS(q, delay, skip));

#endif /* _TRACE_HOOK_BLK_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
=======
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
DECLARE_HOOK(android_vh_blk_fill_rwbs,
	TP_PROTO(char *rwbs, unsigned int opf),
	TP_ARGS(rwbs, opf));

struct path;
struct vfsmount;

DECLARE_HOOK(android_vh_do_new_mount_fc,
	TP_PROTO(struct path *mountpoint, struct vfsmount *mnt),
	TP_ARGS(mountpoint, mnt));

struct readahead_control;
typedef __u32 __bitwise blk_opf_t;

DECLARE_HOOK(android_vh_f2fs_ra_op_flags,
	TP_PROTO(blk_opf_t *op_flag, struct readahead_control *rac),
	TP_ARGS(op_flag, rac));

struct blk_mq_hw_ctx;
struct request_queue;

DECLARE_HOOK(android_vh_blk_mq_delay_run_hw_queue,
	TP_PROTO(int cpu, struct blk_mq_hw_ctx *hctx, unsigned long delay, bool *skip),
	TP_ARGS(cpu, hctx, delay, skip));

DECLARE_HOOK(android_vh_blk_mq_kick_requeue_list,
	TP_PROTO(struct request_queue *q, unsigned long delay, bool *skip),
	TP_ARGS(q, delay, skip));

struct bio;

DECLARE_HOOK(android_vh_check_set_ioprio,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio));

#endif /* _TRACE_HOOK_BLK_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
>>>>>>> CHANGE (52e82fb490db327de31439e47cf58f7bc9a1bb5e ANDROID: vendor_hook: add hooks for I/O priority)
