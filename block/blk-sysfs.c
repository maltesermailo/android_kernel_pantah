// SPDX-License-Identifier: GPL-2.0
/*
 * Functions related to sysfs handling
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/blktrace_api.h>
#include <linux/debugfs.h>
#include <linux/string.h>

#include "blk.h"
#include "blk-mq.h"
#include "blk-mq-debugfs.h"
#include "blk-mq-sched.h"
#include "blk-rq-qos.h"
#include "blk-wbt.h"
#include "blk-cgroup.h"
#include "blk-throttle.h"

struct queue_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct gendisk *disk, char *page);
	int (*load_module)(struct gendisk *disk, const char *page, size_t count);
	ssize_t (*store)(struct gendisk *disk, const char *page, size_t count);
	int (*store_limit)(struct gendisk *disk, const char *page,
			size_t count, struct queue_limits *lim);
};

static ssize_t
queue_var_show(unsigned long var, char *page)
{
	return sprintf(page, "%lu\n", var);
}

static ssize_t
queue_var_store(unsigned long *var, const char *page, size_t count)
{
	int err;
	unsigned long v;

	err = kstrtoul(page, 10, &v);
	if (err || v > UINT_MAX)
		return -EINVAL;

	*var = v;

	return count;
}

static ssize_t queue_requests_show(struct gendisk *disk, char *page)
{
	return queue_var_show(disk->queue->nr_requests, page);
}

static ssize_t
queue_requests_store(struct gendisk *disk, const char *page, size_t count)
{
	unsigned long nr;
	int ret, err;

	if (!queue_is_mq(disk->queue))
		return -EINVAL;

	ret = queue_var_store(&nr, page, count);
	if (ret < 0)
		return ret;

	if (nr < BLKDEV_MIN_RQ)
		nr = BLKDEV_MIN_RQ;

	err = blk_mq_update_nr_requests(disk->queue, nr);
	if (err)
		return err;

	return ret;
}

static ssize_t queue_ra_show(struct gendisk *disk, char *page)
{
	return queue_var_show(disk->bdi->ra_pages << (PAGE_SHIFT - 10), page);
}

static ssize_t
queue_ra_store(struct gendisk *disk, const char *page, size_t count)
{
	unsigned long ra_kb;
	ssize_t ret;

	ret = queue_var_store(&ra_kb, page, count);
	if (ret < 0)
		return ret;
	disk->bdi->ra_pages = ra_kb >> (PAGE_SHIFT - 10);
	return ret;
}

#define QUEUE_SYSFS_LIMIT_SHOW(_field)					\
static ssize_t queue_##_field##_show(struct gendisk *disk, char *page)	\
{									\
	return queue_var_show(disk->queue->limits._field, page);	\
}

QUEUE_SYSFS_LIMIT_SHOW(max_segments)
QUEUE_SYSFS_LIMIT_SHOW(max_discard_segments)
QUEUE_SYSFS_LIMIT_SHOW(max_integrity_segments)
QUEUE_SYSFS_LIMIT_SHOW(max_segment_size)
QUEUE_SYSFS_LIMIT_SHOW(logical_block_size)
QUEUE_SYSFS_LIMIT_SHOW(physical_block_size)
QUEUE_SYSFS_LIMIT_SHOW(chunk_sectors)
QUEUE_SYSFS_LIMIT_SHOW(io_min)
QUEUE_SYSFS_LIMIT_SHOW(io_opt)
QUEUE_SYSFS_LIMIT_SHOW(discard_granularity)
QUEUE_SYSFS_LIMIT_SHOW(zone_write_granularity)
QUEUE_SYSFS_LIMIT_SHOW(virt_boundary_mask)
QUEUE_SYSFS_LIMIT_SHOW(dma_alignment)
QUEUE_SYSFS_LIMIT_SHOW(max_open_zones)
QUEUE_SYSFS_LIMIT_SHOW(max_active_zones)
QUEUE_SYSFS_LIMIT_SHOW(atomic_write_unit_min)
QUEUE_SYSFS_LIMIT_SHOW(atomic_write_unit_max)

#define QUEUE_SYSFS_LIMIT_SHOW_SECTORS_TO_BYTES(_field)			\
static ssize_t queue_##_field##_show(struct gendisk *disk, char *page)	\
{									\
	return sprintf(page, "%llu\n",					\
		(unsigned long long)disk->queue->limits._field <<	\
			SECTOR_SHIFT);					\
}

QUEUE_SYSFS_LIMIT_SHOW_SECTORS_TO_BYTES(max_discard_sectors)
QUEUE_SYSFS_LIMIT_SHOW_SECTORS_TO_BYTES(max_hw_discard_sectors)
QUEUE_SYSFS_LIMIT_SHOW_SECTORS_TO_BYTES(max_write_zeroes_sectors)
QUEUE_SYSFS_LIMIT_SHOW_SECTORS_TO_BYTES(atomic_write_max_sectors)
QUEUE_SYSFS_LIMIT_SHOW_SECTORS_TO_BYTES(atomic_write_boundary_sectors)

#define QUEUE_SYSFS_LIMIT_SHOW_SECTORS_TO_KB(_field)			\
static ssize_t queue_##_field##_show(struct gendisk *disk, char *page)	\
{									\
	return queue_var_show(disk->queue->limits._field >> 1, page);	\
}

QUEUE_SYSFS_LIMIT_SHOW_SECTORS_TO_KB(max_sectors)
QUEUE_SYSFS_LIMIT_SHOW_SECTORS_TO_KB(max_hw_sectors)

#define QUEUE_SYSFS_SHOW_CONST(_name, _val)				\
static ssize_t queue_##_name##_show(struct gendisk *disk, char *page)	\
{									\
	return sprintf(page, "%d\n", _val);				\
}

/* deprecated fields */
QUEUE_SYSFS_SHOW_CONST(discard_zeroes_data, 0)
QUEUE_SYSFS_SHOW_CONST(write_same_max, 0)

static ssize_t queue_poll_delay_show(struct gendisk *disk, char *page)
{
	struct request_queue *q = disk->queue;
	long val;

	if (q->poll_nsec == BLK_MQ_POLL_CLASSIC)
		val = BLK_MQ_POLL_CLASSIC;
	else
		val = q->poll_nsec / 1000;

	return sysfs_emit(page, "%ld\n", val);
}

static int queue_max_discard_sectors_store(struct gendisk *disk,
		const char *page, size_t count, struct queue_limits *lim)
{
	unsigned long max_discard_bytes;
	ssize_t ret;

	ret = queue_var_store(&max_discard_bytes, page, count);
	if (ret < 0)
		return ret;

	if (max_discard_bytes & (disk->queue->limits.discard_granularity - 1))
		return -EINVAL;

	if ((max_discard_bytes >> SECTOR_SHIFT) > UINT_MAX)
		return -EINVAL;

	lim->max_user_discard_sectors = max_discard_bytes >> SECTOR_SHIFT;
	return 0;
}

/*
 * For zone append queue_max_zone_append_sectors does not just return the
 * underlying queue limits, but actually contains a calculation.  Because of
 * that we can't simply use QUEUE_SYSFS_LIMIT_SHOW_SECTORS_TO_BYTES here.
 */
static ssize_t queue_zone_append_max_show(struct gendisk *disk, char *page)
{
	return sprintf(page, "%llu\n",
		(u64)queue_max_zone_append_sectors(disk->queue) <<
			SECTOR_SHIFT);
}

static int
queue_max_sectors_store(struct gendisk *disk, const char *page, size_t count,
		struct queue_limits *lim)
{
	unsigned long max_sectors_kb;
	ssize_t ret;

	ret = queue_var_store(&max_sectors_kb, page, count);
	if (ret < 0)
		return ret;

	lim->max_user_sectors = max_sectors_kb << 1;
	return 0;
}

static ssize_t queue_feature_store(struct gendisk *disk, const char *page,
		size_t count, struct queue_limits *lim, blk_features_t feature)
{
	unsigned long val;
	ssize_t ret;

	ret = queue_var_store(&val, page, count);
	if (ret < 0)
		return ret;

	if (val)
		lim->features |= feature;
	else
		lim->features &= ~feature;
	return 0;
}

#define QUEUE_SYSFS_FEATURE(_name, _feature)				\
static ssize_t queue_##_name##_show(struct gendisk *disk, char *page)	\
{									\
	return sprintf(page, "%u\n",					\
		!!(disk->queue->limits.features & _feature));		\
}									\
static int queue_##_name##_store(struct gendisk *disk,			\
		const char *page, size_t count, struct queue_limits *lim) \
{									\
	return queue_feature_store(disk, page, count, lim, _feature);	\
}

QUEUE_SYSFS_FEATURE(rotational, BLK_FEAT_ROTATIONAL)
QUEUE_SYSFS_FEATURE(add_random, BLK_FEAT_ADD_RANDOM)
QUEUE_SYSFS_FEATURE(iostats, BLK_FEAT_IO_STAT)
QUEUE_SYSFS_FEATURE(stable_writes, BLK_FEAT_STABLE_WRITES);

#define QUEUE_SYSFS_FEATURE_SHOW(_name, _feature)			\
static ssize_t queue_##_name##_show(struct gendisk *disk, char *page)	\
{									\
	return sprintf(page, "%u\n",					\
		!!(disk->queue->limits.features & _feature));		\
}

QUEUE_SYSFS_FEATURE_SHOW(fua, BLK_FEAT_FUA);
QUEUE_SYSFS_FEATURE_SHOW(dax, BLK_FEAT_DAX);

static ssize_t queue_poll_show(struct gendisk *disk, char *page)
{
	if (queue_is_mq(disk->queue))
		return sysfs_emit(page, "%u\n", blk_mq_can_poll(disk->queue));
	return sysfs_emit(page, "%u\n",
		!!(disk->queue->limits.features & BLK_FEAT_POLL));
}

static ssize_t queue_zoned_show(struct gendisk *disk, char *page)
{
	if (blk_queue_is_zoned(disk->queue))
		return sprintf(page, "host-managed\n");
	return sprintf(page, "none\n");
}

static ssize_t queue_nr_zones_show(struct gendisk *disk, char *page)
{
	return queue_var_show(disk_nr_zones(disk), page);
}

static ssize_t queue_nomerges_show(struct gendisk *disk, char *page)
{
	return queue_var_show((blk_queue_nomerges(disk->queue) << 1) |
			       blk_queue_noxmerges(disk->queue), page);
}

static ssize_t queue_nomerges_store(struct gendisk *disk, const char *page,
				    size_t count)
{
	unsigned long nm;
	ssize_t ret = queue_var_store(&nm, page, count);

	if (ret < 0)
		return ret;

	blk_queue_flag_clear(QUEUE_FLAG_NOMERGES, disk->queue);
	blk_queue_flag_clear(QUEUE_FLAG_NOXMERGES, disk->queue);
	if (nm == 2)
		blk_queue_flag_set(QUEUE_FLAG_NOMERGES, disk->queue);
	else if (nm)
		blk_queue_flag_set(QUEUE_FLAG_NOXMERGES, disk->queue);

	return ret;
}

static ssize_t queue_rq_affinity_show(struct gendisk *disk, char *page)
{
	bool set = test_bit(QUEUE_FLAG_SAME_COMP, &disk->queue->queue_flags);
	bool force = test_bit(QUEUE_FLAG_SAME_FORCE, &disk->queue->queue_flags);

	return queue_var_show(set << force, page);
}

static ssize_t
queue_rq_affinity_store(struct gendisk *disk, const char *page, size_t count)
{
	ssize_t ret = -EINVAL;
#ifdef CONFIG_SMP
	struct request_queue *q = disk->queue;
	unsigned long val;

	ret = queue_var_store(&val, page, count);
	if (ret < 0)
		return ret;

	if (val == 2) {
		blk_queue_flag_set(QUEUE_FLAG_SAME_COMP, q);
		blk_queue_flag_set(QUEUE_FLAG_SAME_FORCE, q);
	} else if (val == 1) {
		blk_queue_flag_set(QUEUE_FLAG_SAME_COMP, q);
		blk_queue_flag_clear(QUEUE_FLAG_SAME_FORCE, q);
	} else if (val == 0) {
		blk_queue_flag_clear(QUEUE_FLAG_SAME_COMP, q);
		blk_queue_flag_clear(QUEUE_FLAG_SAME_FORCE, q);
	}
#endif
	return ret;
}

static ssize_t queue_poll_delay_store(struct gendisk *disk, const char *page,
				size_t count)
{
	struct request_queue *q = disk->queue;
	long val;
	int ret;

	ret = kstrtol(page, 10, &val);
	if (ret < 0)
		return ret;

	if (val == -1)
		q->poll_nsec = BLK_MQ_POLL_CLASSIC;
	else if (val >= 0)
		q->poll_nsec = val * 1000;
	else
		return -EINVAL;

	return count;
}

static ssize_t queue_dpas_int_show(char *page, int val)
{
	return sysfs_emit(page, "%d\n", val);
}

static ssize_t queue_dpas_ll_show(char *page, long long val)
{
	return sysfs_emit(page, "%lld\n", val);
}

static ssize_t queue_dpas_u32_show(char *page, u32 val)
{
	return sysfs_emit(page, "%u\n", val);
}

static ssize_t queue_dpas_int_store(const char *page, size_t count, int *field)
{
	int val;
	int ret;

	ret = kstrtoint(page, 10, &val);
	if (ret < 0)
		return ret;
	*field = val;
	return count;
}

static ssize_t queue_dpas_ll_store(const char *page, size_t count,
				   long long *field)
{
	long long val;
	int ret;

	ret = kstrtoll(page, 10, &val);
	if (ret < 0)
		return ret;
	*field = val;
	return count;
}

static void queue_dpas_reinit_pas_stat(struct blk_rq_pas_stat *stat, u32 dur,
				       long long adj, long long up,
				       long long dn)
{
	stat->dur = dur;
	stat->adj = adj;
	stat->up = up;
	stat->dn = dn;
	stat->sr_pnlt = 0;
	stat->sr_last = 1;
	stat->update_req = 0;
	stat->dur_cnt = 1;
	stat->dur_cnt_checked = 0;
}

static void queue_dpas_reinit_pas_stats(struct request_queue *q, long long adj)
{
	int bucket;
	int cpu;

	if (!q->pas_stat)
		return;

	for_each_possible_cpu(cpu) {
		struct blk_rq_pas_stat *stat = per_cpu_ptr(q->pas_stat, cpu);

		for (bucket = 0; bucket < BLK_MQ_POLL_STATS_BKTS; bucket++)
			queue_dpas_reinit_pas_stat(&stat[bucket], q->d_init,
						   adj, q->up_init,
						   q->dn_init);
	}
}

static void queue_dpas_sync_switch_params(struct request_queue *q)
{
	int cpu;

	if (!irq_poll_switch)
		return;

	for_each_possible_cpu(cpu) {
		struct blk_switch *sc = per_cpu_ptr(irq_poll_switch, cpu);

		sc->enabled = q->switch_enabled;
		sc->param1 = q->switch_param1;
		sc->param2 = q->switch_param2;
		sc->param3 = q->switch_param3;
		sc->param4 = q->switch_param4;
		sc->param5 = q->switch_param5;
		sc->param6 = q->switch_param6;
		sc->param7 = q->switch_param7;
	}
}

static void queue_dpas_reset_switch_state(struct request_queue *q)
{
	int cpu;

	if (!irq_poll_switch)
		return;

	for_each_possible_cpu(cpu) {
		struct blk_switch *sc = per_cpu_ptr(irq_poll_switch, cpu);

		sc->enabled = q->switch_enabled;
		/* Keep reset semantics aligned with blk-mq.c init: 2 == _PAS. */
		sc->mode = 2;
		sc->cp_cnt = 0;
		sc->pas_cnt = 0;
		sc->ol_cnt = 0;
		sc->int_cnt = 0;
		sc->cp_tot = 0;
		sc->pas_tot = 0;
		sc->ol_tot = 0;
		sc->int_tot = 0;
		sc->N_POLL = 0;
		sc->N_INT = 10000;
		sc->N_PAS = 0;
		sc->ioctr = 0;
		sc->qd = 0;
		sc->qd_sum = 0;
		sc->tf = 0;
		sc->param1 = q->switch_param1;
		sc->param2 = q->switch_param2;
		sc->param3 = q->switch_param3;
		sc->param4 = q->switch_param4;
		sc->param5 = q->switch_param5;
		sc->param6 = q->switch_param6;
		sc->param7 = q->switch_param7;
	}
	queue_dpas_reinit_pas_stats(q, q->div);
}

static ssize_t queue_dpas_switch_store(struct gendisk *disk, const char *page,
				       size_t count, int *field)
{
	struct request_queue *q = disk->queue;
	int val;
	int ret;

	ret = kstrtoint(page, 10, &val);
	if (ret < 0)
		return ret;
	if (val < -1)
		return -EINVAL;

	*field = val;
	queue_dpas_sync_switch_params(q);
	return count;
}

#define QUEUE_DPAS_INT_ATTR(_name, _field)				\
static ssize_t queue_##_name##_show(struct gendisk *disk, char *page)	\
{									\
	return queue_dpas_int_show(page, disk->queue->_field);		\
}									\
static ssize_t queue_##_name##_store(struct gendisk *disk,		\
				     const char *page, size_t count)	\
{									\
	return queue_dpas_int_store(page, count, &disk->queue->_field);	\
}

#define QUEUE_DPAS_LL_ATTR(_name, _field)				\
static ssize_t queue_##_name##_show(struct gendisk *disk, char *page)	\
{									\
	return queue_dpas_ll_show(page, disk->queue->_field);		\
}									\
static ssize_t queue_##_name##_store(struct gendisk *disk,		\
				     const char *page, size_t count)	\
{									\
	return queue_dpas_ll_store(page, count, &disk->queue->_field);	\
}

#define QUEUE_DPAS_SWITCH_ATTR(_name, _field)					\
static ssize_t queue_##_name##_show(struct gendisk *disk, char *page)		\
{										\
	return queue_dpas_int_show(page, disk->queue->_field);			\
}										\
static ssize_t queue_##_name##_store(struct gendisk *disk,			\
				     const char *page, size_t count)		\
{										\
	return queue_dpas_switch_store(disk, page, count,			\
				       &disk->queue->_field);			\
}

QUEUE_DPAS_INT_ATTR(pas_enabled, pas_enabled);
QUEUE_DPAS_INT_ATTR(pas_adaptive_enabled, pas_adaptive_enabled);
QUEUE_DPAS_INT_ATTR(ehp_enabled, ehp_enabled);
QUEUE_DPAS_INT_ATTR(max_no_lock, max_no_lock);
QUEUE_DPAS_INT_ATTR(poll_threshold, poll_threshold);
QUEUE_DPAS_INT_ATTR(logging_enabled, logging_enabled);
QUEUE_DPAS_INT_ATTR(buffered_poll_enabled, buffered_poll_enabled);
QUEUE_DPAS_INT_ATTR(buffered_poll_readahead, buffered_poll_readahead);
QUEUE_DPAS_SWITCH_ATTR(switch_enabled, switch_enabled);
QUEUE_DPAS_SWITCH_ATTR(switch_param1, switch_param1);
QUEUE_DPAS_SWITCH_ATTR(switch_param2, switch_param2);
QUEUE_DPAS_SWITCH_ATTR(switch_param3, switch_param3);
QUEUE_DPAS_SWITCH_ATTR(switch_param4, switch_param4);
QUEUE_DPAS_SWITCH_ATTR(switch_param5, switch_param5);
QUEUE_DPAS_SWITCH_ATTR(switch_param6, switch_param6);
QUEUE_DPAS_SWITCH_ATTR(switch_param7, switch_param7);
QUEUE_DPAS_LL_ATTR(heat_up, heat_up);
QUEUE_DPAS_LL_ATTR(cool_dn, cool_dn);
QUEUE_DPAS_LL_ATTR(min_dn, min_dn);
QUEUE_DPAS_LL_ATTR(max_dn, max_dn);

static ssize_t queue_d_init_show(struct gendisk *disk, char *page)
{
	return queue_dpas_u32_show(page, disk->queue->d_init);
}

static ssize_t queue_d_init_store(struct gendisk *disk, const char *page,
				  size_t count)
{
	struct request_queue *q = disk->queue;
	u32 val;
	int ret;

	ret = kstrtou32(page, 10, &val);
	if (ret < 0)
		return ret;
	if (val < 100 || val > 99000)
		return -EINVAL;

	q->d_init = val;
	queue_dpas_reinit_pas_stats(q, q->div);
	return count;
}

static ssize_t queue_up_init_show(struct gendisk *disk, char *page)
{
	return queue_dpas_ll_show(page, disk->queue->up_init);
}

static ssize_t queue_up_init_store(struct gendisk *disk, const char *page,
				   size_t count)
{
	struct request_queue *q = disk->queue;
	long long val;
	int ret;

	ret = kstrtoll(page, 10, &val);
	if (ret < 0)
		return ret;
	if (val < 100 || val > 99000)
		return -EINVAL;

	q->up_init = val;
	if (q->pas_adaptive_enabled == 1) {
		q->dn_init = q->up_init * q->updn_ratio;
		if (q->dn_init > q->max_dn) {
			q->dn_init = q->max_dn;
			q->up_init = q->dn_init / q->updn_ratio;
		} else if (q->dn_init < q->min_dn) {
			q->dn_init = q->min_dn;
			q->up_init = q->dn_init / q->updn_ratio;
		}
	}
	queue_dpas_reinit_pas_stats(q, q->div + q->up_init);
	return count;
}

static ssize_t queue_dn_init_show(struct gendisk *disk, char *page)
{
	return queue_dpas_ll_show(page, disk->queue->dn_init);
}

static ssize_t queue_dn_init_store(struct gendisk *disk, const char *page,
				   size_t count)
{
	struct request_queue *q = disk->queue;
	long long val;
	int ret;

	ret = kstrtoll(page, 10, &val);
	if (ret < 0)
		return ret;
	if (val < 10000 || val > 990000)
		return -EINVAL;

	q->dn_init = val;
	if (q->pas_adaptive_enabled == 1) {
		if (q->dn_init > q->max_dn) {
			q->dn_init = q->max_dn;
			q->up_init = q->dn_init / q->updn_ratio;
		} else if (q->dn_init < q->min_dn) {
			q->dn_init = q->min_dn;
			q->up_init = q->dn_init / q->updn_ratio;
		}
	}
	queue_dpas_reinit_pas_stats(q, q->div + q->up_init);
	return count;
}

static ssize_t queue_pas_exception_show(struct gendisk *disk, char *page)
{
	struct request_queue *q = disk->queue;

	return sysfs_emit(page,
			  "hybrid_poll=%llu fops=%llu comp_before_sleep=%llu lock_d_c_separate=%llu\n",
			  q->cnt_rel_hybrid_poll, q->cnt_rel_fops,
			  q->cnt_rel_comp_before_sleep,
			  q->cnt_lock_d_c_separate);
}

static ssize_t queue_pas_exception_store(struct gendisk *disk,
					 const char *page, size_t count)
{
	struct request_queue *q = disk->queue;
	unsigned int val;
	int ret;

	ret = kstrtouint(page, 10, &val);
	if (ret < 0)
		return ret;
	if (val) {
		q->cnt_rel_hybrid_poll = 0;
		q->cnt_rel_fops = 0;
		q->cnt_rel_comp_before_sleep = 0;
		q->cnt_lock_d_c_separate = 0;
	}
	return count;
}

static ssize_t queue_buffered_poll_comm_show(struct gendisk *disk, char *page)
{
	return sysfs_emit(page, "%s\n", disk->queue->buffered_poll_comm);
}

static ssize_t queue_buffered_poll_comm_store(struct gendisk *disk,
					      const char *page, size_t count)
{
	struct request_queue *q = disk->queue;
	size_t len = strnlen(page, count);

	if (len && page[len - 1] == '\n')
		len--;
	if (len >= TASK_COMM_LEN)
		return -EINVAL;

	memset(q->buffered_poll_comm, 0, sizeof(q->buffered_poll_comm));
	memcpy(q->buffered_poll_comm, page, len);
	return count;
}

static ssize_t queue_buffered_poll_stat_show(struct gendisk *disk, char *page)
{
	struct request_queue *q = disk->queue;

	return sysfs_emit(page,
			  "enabled=%d readahead=%d comm=%s selected=%lld completed=%lld loops=%lld timeout=%lld ra_skip=%lld skip=%lld\n",
			  q->buffered_poll_enabled, q->buffered_poll_readahead,
			  q->buffered_poll_comm,
			  atomic64_read(&q->buffered_poll_selected),
			  atomic64_read(&q->buffered_poll_completed),
			  atomic64_read(&q->buffered_poll_loops),
			  atomic64_read(&q->buffered_poll_timeout),
			  atomic64_read(&q->buffered_poll_ra_skip),
			  atomic64_read(&q->buffered_poll_skip));
}

static ssize_t queue_buffered_poll_reset_show(struct gendisk *disk, char *page)
{
	return sysfs_emit(page, "0\n");
}

static ssize_t queue_buffered_poll_reset_store(struct gendisk *disk,
					       const char *page, size_t count)
{
	struct request_queue *q = disk->queue;
	unsigned int val;
	int ret;

	ret = kstrtouint(page, 10, &val);
	if (ret < 0)
		return ret;
	if (!val)
		return count;

	atomic64_set(&q->buffered_poll_selected, 0);
	atomic64_set(&q->buffered_poll_completed, 0);
	atomic64_set(&q->buffered_poll_loops, 0);
	atomic64_set(&q->buffered_poll_timeout, 0);
	atomic64_set(&q->buffered_poll_ra_skip, 0);
	atomic64_set(&q->buffered_poll_skip, 0);
	return count;
}

static ssize_t queue_switch_reset_show(struct gendisk *disk, char *page)
{
	return sysfs_emit(page, "0\n");
}

static ssize_t queue_switch_reset_store(struct gendisk *disk,
					const char *page, size_t count)
{
	unsigned int val;
	int ret;

	ret = kstrtouint(page, 10, &val);
	if (ret < 0)
		return ret;
	if (!val)
		return count;

	queue_dpas_reset_switch_state(disk->queue);
	return count;
}

static ssize_t queue_switch_stat_show(struct gendisk *disk, char *page)
{
	int cpu;
	ssize_t len = 0;

	if (!irq_poll_switch)
		return sysfs_emit(page, "irq_poll_switch=uninitialized\n");

	for_each_possible_cpu(cpu) {
		struct blk_switch *sc = per_cpu_ptr(irq_poll_switch, cpu);

		len += sysfs_emit_at(page, len,
				     "cpu=%d enabled=%d mode=%d param1=%d param2=%d param3=%d param4=%d param5=%d param6=%d param7=%d cp=%llu pas=%llu ol=%llu int=%llu qd=%d\n",
				     cpu, sc->enabled, sc->mode, sc->param1,
				     sc->param2, sc->param3, sc->param4,
				     sc->param5, sc->param6, sc->param7,
				     sc->cp_tot, sc->pas_tot, sc->ol_tot,
				     sc->int_tot, sc->qd);
	}
	return len;
}

static ssize_t queue_poll_store(struct gendisk *disk, const char *page,
				size_t count)
{
	if (!(disk->queue->limits.features & BLK_FEAT_POLL))
		return -EINVAL;
	pr_info_ratelimited("writes to the poll attribute are ignored.\n");
	pr_info_ratelimited("please use driver specific parameters instead.\n");
	return count;
}

static ssize_t queue_io_timeout_show(struct gendisk *disk, char *page)
{
	return sprintf(page, "%u\n", jiffies_to_msecs(disk->queue->rq_timeout));
}

static ssize_t queue_io_timeout_store(struct gendisk *disk, const char *page,
				  size_t count)
{
	unsigned int val;
	int err;

	err = kstrtou32(page, 10, &val);
	if (err || val == 0)
		return -EINVAL;

	blk_queue_rq_timeout(disk->queue, msecs_to_jiffies(val));

	return count;
}

static ssize_t queue_wc_show(struct gendisk *disk, char *page)
{
	if (blk_queue_write_cache(disk->queue))
		return sprintf(page, "write back\n");
	return sprintf(page, "write through\n");
}

static int queue_wc_store(struct gendisk *disk, const char *page,
		size_t count, struct queue_limits *lim)
{
	bool disable;

	if (!strncmp(page, "write back", 10)) {
		disable = false;
	} else if (!strncmp(page, "write through", 13) ||
		   !strncmp(page, "none", 4)) {
		disable = true;
	} else {
		return -EINVAL;
	}

	if (disable)
		lim->flags |= BLK_FLAG_WRITE_CACHE_DISABLED;
	else
		lim->flags &= ~BLK_FLAG_WRITE_CACHE_DISABLED;
	return 0;
}

#define QUEUE_RO_ENTRY(_prefix, _name)			\
static struct queue_sysfs_entry _prefix##_entry = {	\
	.attr	= { .name = _name, .mode = 0444 },	\
	.show	= _prefix##_show,			\
};

#define QUEUE_RW_ENTRY(_prefix, _name)			\
static struct queue_sysfs_entry _prefix##_entry = {	\
	.attr	= { .name = _name, .mode = 0644 },	\
	.show	= _prefix##_show,			\
	.store	= _prefix##_store,			\
};

#define QUEUE_LIM_RW_ENTRY(_prefix, _name)			\
static struct queue_sysfs_entry _prefix##_entry = {	\
	.attr		= { .name = _name, .mode = 0644 },	\
	.show		= _prefix##_show,			\
	.store_limit	= _prefix##_store,			\
}

#define QUEUE_RW_LOAD_MODULE_ENTRY(_prefix, _name)		\
static struct queue_sysfs_entry _prefix##_entry = {		\
	.attr		= { .name = _name, .mode = 0644 },	\
	.show		= _prefix##_show,			\
	.load_module	= _prefix##_load_module,		\
	.store		= _prefix##_store,			\
}

QUEUE_RW_ENTRY(queue_requests, "nr_requests");
QUEUE_RW_ENTRY(queue_ra, "read_ahead_kb");
QUEUE_LIM_RW_ENTRY(queue_max_sectors, "max_sectors_kb");
QUEUE_RO_ENTRY(queue_max_hw_sectors, "max_hw_sectors_kb");
QUEUE_RO_ENTRY(queue_max_segments, "max_segments");
QUEUE_RO_ENTRY(queue_max_integrity_segments, "max_integrity_segments");
QUEUE_RO_ENTRY(queue_max_segment_size, "max_segment_size");
QUEUE_RW_LOAD_MODULE_ENTRY(elv_iosched, "scheduler");

QUEUE_RO_ENTRY(queue_logical_block_size, "logical_block_size");
QUEUE_RO_ENTRY(queue_physical_block_size, "physical_block_size");
QUEUE_RO_ENTRY(queue_chunk_sectors, "chunk_sectors");
QUEUE_RO_ENTRY(queue_io_min, "minimum_io_size");
QUEUE_RO_ENTRY(queue_io_opt, "optimal_io_size");

QUEUE_RO_ENTRY(queue_max_discard_segments, "max_discard_segments");
QUEUE_RO_ENTRY(queue_discard_granularity, "discard_granularity");
QUEUE_RO_ENTRY(queue_max_hw_discard_sectors, "discard_max_hw_bytes");
QUEUE_LIM_RW_ENTRY(queue_max_discard_sectors, "discard_max_bytes");
QUEUE_RO_ENTRY(queue_discard_zeroes_data, "discard_zeroes_data");

QUEUE_RO_ENTRY(queue_atomic_write_max_sectors, "atomic_write_max_bytes");
QUEUE_RO_ENTRY(queue_atomic_write_boundary_sectors,
		"atomic_write_boundary_bytes");
QUEUE_RO_ENTRY(queue_atomic_write_unit_max, "atomic_write_unit_max_bytes");
QUEUE_RO_ENTRY(queue_atomic_write_unit_min, "atomic_write_unit_min_bytes");

QUEUE_RO_ENTRY(queue_write_same_max, "write_same_max_bytes");
QUEUE_RO_ENTRY(queue_max_write_zeroes_sectors, "write_zeroes_max_bytes");
QUEUE_RO_ENTRY(queue_zone_append_max, "zone_append_max_bytes");
QUEUE_RO_ENTRY(queue_zone_write_granularity, "zone_write_granularity");

QUEUE_RO_ENTRY(queue_zoned, "zoned");
QUEUE_RO_ENTRY(queue_nr_zones, "nr_zones");
QUEUE_RO_ENTRY(queue_max_open_zones, "max_open_zones");
QUEUE_RO_ENTRY(queue_max_active_zones, "max_active_zones");

QUEUE_RW_ENTRY(queue_nomerges, "nomerges");
QUEUE_RW_ENTRY(queue_rq_affinity, "rq_affinity");
QUEUE_RW_ENTRY(queue_poll, "io_poll");
QUEUE_RW_ENTRY(queue_poll_delay, "io_poll_delay");
QUEUE_RW_ENTRY(queue_pas_enabled, "pas_enabled");
QUEUE_RW_ENTRY(queue_pas_adaptive_enabled, "pas_adaptive_enabled");
QUEUE_RO_ENTRY(queue_switch_stat, "switch_stat");
QUEUE_RW_ENTRY(queue_switch_reset, "switch_reset");
QUEUE_RW_ENTRY(queue_switch_param1, "switch_param1");
QUEUE_RW_ENTRY(queue_switch_param2, "switch_param2");
QUEUE_RW_ENTRY(queue_switch_param3, "switch_param3");
QUEUE_RW_ENTRY(queue_switch_param4, "switch_param4");
QUEUE_RW_ENTRY(queue_switch_param5, "switch_param5");
QUEUE_RW_ENTRY(queue_switch_param6, "switch_param6");
QUEUE_RW_ENTRY(queue_switch_param7, "switch_param7");
QUEUE_RW_ENTRY(queue_switch_enabled, "switch_enabled");
QUEUE_RW_ENTRY(queue_ehp_enabled, "ehp_enabled");
QUEUE_RW_ENTRY(queue_max_no_lock, "pas_max_no_lock");
QUEUE_RW_ENTRY(queue_poll_threshold, "pas_poll_threshold");
QUEUE_RW_ENTRY(queue_pas_exception, "pas_exception");
QUEUE_RW_ENTRY(queue_d_init, "pas_d_init");
QUEUE_RW_ENTRY(queue_up_init, "pas_up_init");
QUEUE_RW_ENTRY(queue_dn_init, "pas_dn_init");
QUEUE_RW_ENTRY(queue_heat_up, "pas_heat_up");
QUEUE_RW_ENTRY(queue_cool_dn, "pas_cool_dn");
QUEUE_RW_ENTRY(queue_min_dn, "pas_min_dn");
QUEUE_RW_ENTRY(queue_max_dn, "pas_max_dn");
QUEUE_RW_ENTRY(queue_logging_enabled, "logging_enabled");
QUEUE_RW_ENTRY(queue_buffered_poll_enabled, "buffered_poll_enabled");
QUEUE_RW_ENTRY(queue_buffered_poll_readahead, "buffered_poll_readahead");
QUEUE_RW_ENTRY(queue_buffered_poll_comm, "buffered_poll_comm");
QUEUE_RO_ENTRY(queue_buffered_poll_stat, "buffered_poll_stat");
QUEUE_RW_ENTRY(queue_buffered_poll_reset, "buffered_poll_reset");
QUEUE_LIM_RW_ENTRY(queue_wc, "write_cache");
QUEUE_RO_ENTRY(queue_fua, "fua");
QUEUE_RO_ENTRY(queue_dax, "dax");
QUEUE_RW_ENTRY(queue_io_timeout, "io_timeout");
QUEUE_RO_ENTRY(queue_virt_boundary_mask, "virt_boundary_mask");
QUEUE_RO_ENTRY(queue_dma_alignment, "dma_alignment");

/* legacy alias for logical_block_size: */
static struct queue_sysfs_entry queue_hw_sector_size_entry = {
	.attr = {.name = "hw_sector_size", .mode = 0444 },
	.show = queue_logical_block_size_show,
};

QUEUE_LIM_RW_ENTRY(queue_rotational, "rotational");
QUEUE_LIM_RW_ENTRY(queue_iostats, "iostats");
QUEUE_LIM_RW_ENTRY(queue_add_random, "add_random");
QUEUE_LIM_RW_ENTRY(queue_stable_writes, "stable_writes");

#ifdef CONFIG_BLK_WBT
static ssize_t queue_var_store64(s64 *var, const char *page)
{
	int err;
	s64 v;

	err = kstrtos64(page, 10, &v);
	if (err < 0)
		return err;

	*var = v;
	return 0;
}

static ssize_t queue_wb_lat_show(struct gendisk *disk, char *page)
{
	if (!wbt_rq_qos(disk->queue))
		return -EINVAL;

	if (wbt_disabled(disk->queue))
		return sprintf(page, "0\n");

	return sprintf(page, "%llu\n",
		div_u64(wbt_get_min_lat(disk->queue), 1000));
}

static ssize_t queue_wb_lat_store(struct gendisk *disk, const char *page,
				  size_t count)
{
	struct request_queue *q = disk->queue;
	struct rq_qos *rqos;
	ssize_t ret;
	s64 val;

	ret = queue_var_store64(&val, page);
	if (ret < 0)
		return ret;
	if (val < -1)
		return -EINVAL;

	rqos = wbt_rq_qos(q);
	if (!rqos) {
		ret = wbt_init(disk);
		if (ret)
			return ret;
	}

	if (val == -1)
		val = wbt_default_latency_nsec(q);
	else if (val >= 0)
		val *= 1000ULL;

	if (wbt_get_min_lat(q) == val)
		return count;

	/*
	 * Ensure that the queue is idled, in case the latency update
	 * ends up either enabling or disabling wbt completely. We can't
	 * have IO inflight if that happens.
	 */
	blk_mq_quiesce_queue(q);

	wbt_set_min_lat(q, val);

	blk_mq_unquiesce_queue(q);

	return count;
}

QUEUE_RW_ENTRY(queue_wb_lat, "wbt_lat_usec");
#endif

/* Common attributes for bio-based and request-based queues. */
static struct attribute *queue_attrs[] = {
	&queue_ra_entry.attr,
	&queue_max_hw_sectors_entry.attr,
	&queue_max_sectors_entry.attr,
	&queue_max_segments_entry.attr,
	&queue_max_discard_segments_entry.attr,
	&queue_max_integrity_segments_entry.attr,
	&queue_max_segment_size_entry.attr,
	&queue_hw_sector_size_entry.attr,
	&queue_logical_block_size_entry.attr,
	&queue_physical_block_size_entry.attr,
	&queue_chunk_sectors_entry.attr,
	&queue_io_min_entry.attr,
	&queue_io_opt_entry.attr,
	&queue_discard_granularity_entry.attr,
	&queue_max_discard_sectors_entry.attr,
	&queue_max_hw_discard_sectors_entry.attr,
	&queue_discard_zeroes_data_entry.attr,
	&queue_atomic_write_max_sectors_entry.attr,
	&queue_atomic_write_boundary_sectors_entry.attr,
	&queue_atomic_write_unit_min_entry.attr,
	&queue_atomic_write_unit_max_entry.attr,
	&queue_write_same_max_entry.attr,
	&queue_max_write_zeroes_sectors_entry.attr,
	&queue_zone_append_max_entry.attr,
	&queue_zone_write_granularity_entry.attr,
	&queue_rotational_entry.attr,
	&queue_zoned_entry.attr,
	&queue_nr_zones_entry.attr,
	&queue_max_open_zones_entry.attr,
	&queue_max_active_zones_entry.attr,
	&queue_nomerges_entry.attr,
	&queue_iostats_entry.attr,
	&queue_stable_writes_entry.attr,
	&queue_add_random_entry.attr,
	&queue_poll_entry.attr,
	&queue_wc_entry.attr,
	&queue_fua_entry.attr,
	&queue_dax_entry.attr,
	&queue_poll_delay_entry.attr,
	&queue_pas_enabled_entry.attr,
	&queue_pas_adaptive_enabled_entry.attr,
	&queue_switch_stat_entry.attr,
	&queue_switch_reset_entry.attr,
	&queue_switch_param1_entry.attr,
	&queue_switch_param2_entry.attr,
	&queue_switch_param3_entry.attr,
	&queue_switch_param4_entry.attr,
	&queue_switch_param5_entry.attr,
	&queue_switch_param6_entry.attr,
	&queue_switch_param7_entry.attr,
	&queue_switch_enabled_entry.attr,
	&queue_ehp_enabled_entry.attr,
	&queue_max_no_lock_entry.attr,
	&queue_poll_threshold_entry.attr,
	&queue_pas_exception_entry.attr,
	&queue_d_init_entry.attr,
	&queue_up_init_entry.attr,
	&queue_dn_init_entry.attr,
	&queue_heat_up_entry.attr,
	&queue_cool_dn_entry.attr,
	&queue_min_dn_entry.attr,
	&queue_max_dn_entry.attr,
	&queue_logging_enabled_entry.attr,
	&queue_buffered_poll_enabled_entry.attr,
	&queue_buffered_poll_readahead_entry.attr,
	&queue_buffered_poll_comm_entry.attr,
	&queue_buffered_poll_stat_entry.attr,
	&queue_buffered_poll_reset_entry.attr,
	&queue_virt_boundary_mask_entry.attr,
	&queue_dma_alignment_entry.attr,
	NULL,
};

/* Request-based queue attributes that are not relevant for bio-based queues. */
static struct attribute *blk_mq_queue_attrs[] = {
	&queue_requests_entry.attr,
	&elv_iosched_entry.attr,
	&queue_rq_affinity_entry.attr,
	&queue_io_timeout_entry.attr,
#ifdef CONFIG_BLK_WBT
	&queue_wb_lat_entry.attr,
#endif
	NULL,
};

static umode_t queue_attr_visible(struct kobject *kobj, struct attribute *attr,
				int n)
{
	struct gendisk *disk = container_of(kobj, struct gendisk, queue_kobj);
	struct request_queue *q = disk->queue;

	if ((attr == &queue_max_open_zones_entry.attr ||
	     attr == &queue_max_active_zones_entry.attr) &&
	    !blk_queue_is_zoned(q))
		return 0;

	return attr->mode;
}

static umode_t blk_mq_queue_attr_visible(struct kobject *kobj,
					 struct attribute *attr, int n)
{
	struct gendisk *disk = container_of(kobj, struct gendisk, queue_kobj);
	struct request_queue *q = disk->queue;

	if (!queue_is_mq(q))
		return 0;

	if (attr == &queue_io_timeout_entry.attr && !q->mq_ops->timeout)
		return 0;

	return attr->mode;
}

static struct attribute_group queue_attr_group = {
	.attrs = queue_attrs,
	.is_visible = queue_attr_visible,
};

static struct attribute_group blk_mq_queue_attr_group = {
	.attrs = blk_mq_queue_attrs,
	.is_visible = blk_mq_queue_attr_visible,
};

#define to_queue(atr) container_of((atr), struct queue_sysfs_entry, attr)

static ssize_t
queue_attr_show(struct kobject *kobj, struct attribute *attr, char *page)
{
	struct queue_sysfs_entry *entry = to_queue(attr);
	struct gendisk *disk = container_of(kobj, struct gendisk, queue_kobj);
	ssize_t res;

	if (!entry->show)
		return -EIO;
	mutex_lock(&disk->queue->sysfs_lock);
	res = entry->show(disk, page);
	mutex_unlock(&disk->queue->sysfs_lock);
	return res;
}

static ssize_t
queue_attr_store(struct kobject *kobj, struct attribute *attr,
		    const char *page, size_t length)
{
	struct queue_sysfs_entry *entry = to_queue(attr);
	struct gendisk *disk = container_of(kobj, struct gendisk, queue_kobj);
	struct request_queue *q = disk->queue;
	ssize_t res;

	if (!entry->store_limit && !entry->store)
		return -EIO;

	/*
	 * If the attribute needs to load a module, do it before freezing the
	 * queue to ensure that the module file can be read when the request
	 * queue is the one for the device storing the module file.
	 */
	if (entry->load_module) {
		res = entry->load_module(disk, page, length);
		if (res)
			return res;
	}

	if (entry->store_limit) {
		struct queue_limits lim = queue_limits_start_update(q);

		res = entry->store_limit(disk, page, length, &lim);
		if (res < 0) {
			queue_limits_cancel_update(q);
			return res;
		}

		res = queue_limits_commit_update_frozen(q, &lim);
		if (res)
			return res;
		return length;
	}

	mutex_lock(&q->sysfs_lock);
	blk_mq_freeze_queue(q);
	res = entry->store(disk, page, length);
	blk_mq_unfreeze_queue(q);
	mutex_unlock(&q->sysfs_lock);
	return res;
}

static const struct sysfs_ops queue_sysfs_ops = {
	.show	= queue_attr_show,
	.store	= queue_attr_store,
};

static const struct attribute_group *blk_queue_attr_groups[] = {
	&queue_attr_group,
	&blk_mq_queue_attr_group,
	NULL
};

static void blk_queue_release(struct kobject *kobj)
{
	/* nothing to do here, all data is associated with the parent gendisk */
}

static const struct kobj_type blk_queue_ktype = {
	.default_groups = blk_queue_attr_groups,
	.sysfs_ops	= &queue_sysfs_ops,
	.release	= blk_queue_release,
};

static void blk_debugfs_remove(struct gendisk *disk)
{
	struct request_queue *q = disk->queue;

	mutex_lock(&q->debugfs_mutex);
	blk_trace_shutdown(q);
	debugfs_remove_recursive(q->debugfs_dir);
	q->debugfs_dir = NULL;
	q->sched_debugfs_dir = NULL;
	q->rqos_debugfs_dir = NULL;
	mutex_unlock(&q->debugfs_mutex);
}

/**
 * blk_register_queue - register a block layer queue with sysfs
 * @disk: Disk of which the request queue should be registered with sysfs.
 */
int blk_register_queue(struct gendisk *disk)
{
	struct request_queue *q = disk->queue;
	int ret;

	mutex_lock(&q->sysfs_dir_lock);
	kobject_init(&disk->queue_kobj, &blk_queue_ktype);
	ret = kobject_add(&disk->queue_kobj, &disk_to_dev(disk)->kobj, "queue");
	if (ret < 0)
		goto out_put_queue_kobj;

	if (queue_is_mq(q)) {
		ret = blk_mq_sysfs_register(disk);
		if (ret)
			goto out_put_queue_kobj;
	}
	mutex_lock(&q->sysfs_lock);

	mutex_lock(&q->debugfs_mutex);
	q->debugfs_dir = debugfs_create_dir(disk->disk_name, blk_debugfs_root);
	if (queue_is_mq(q))
		blk_mq_debugfs_register(q);
	mutex_unlock(&q->debugfs_mutex);

	ret = disk_register_independent_access_ranges(disk);
	if (ret)
		goto out_debugfs_remove;

	if (q->elevator) {
		ret = elv_register_queue(q, false);
		if (ret)
			goto out_unregister_ia_ranges;
	}

	ret = blk_crypto_sysfs_register(disk);
	if (ret)
		goto out_elv_unregister;

	blk_queue_flag_set(QUEUE_FLAG_REGISTERED, q);
	wbt_enable_default(disk);

	/* Now everything is ready and send out KOBJ_ADD uevent */
	kobject_uevent(&disk->queue_kobj, KOBJ_ADD);
	if (q->elevator)
		kobject_uevent(&q->elevator->kobj, KOBJ_ADD);
	mutex_unlock(&q->sysfs_lock);
	mutex_unlock(&q->sysfs_dir_lock);

	/*
	 * SCSI probing may synchronously create and destroy a lot of
	 * request_queues for non-existent devices.  Shutting down a fully
	 * functional queue takes measureable wallclock time as RCU grace
	 * periods are involved.  To avoid excessive latency in these
	 * cases, a request_queue starts out in a degraded mode which is
	 * faster to shut down and is made fully functional here as
	 * request_queues for non-existent devices never get registered.
	 */
	blk_queue_flag_set(QUEUE_FLAG_INIT_DONE, q);
	percpu_ref_switch_to_percpu(&q->q_usage_counter);

	return ret;

out_elv_unregister:
	elv_unregister_queue(q);
out_unregister_ia_ranges:
	disk_unregister_independent_access_ranges(disk);
out_debugfs_remove:
	blk_debugfs_remove(disk);
	mutex_unlock(&q->sysfs_lock);
out_put_queue_kobj:
	kobject_put(&disk->queue_kobj);
	mutex_unlock(&q->sysfs_dir_lock);
	return ret;
}

/**
 * blk_unregister_queue - counterpart of blk_register_queue()
 * @disk: Disk of which the request queue should be unregistered from sysfs.
 *
 * Note: the caller is responsible for guaranteeing that this function is called
 * after blk_register_queue() has finished.
 */
void blk_unregister_queue(struct gendisk *disk)
{
	struct request_queue *q = disk->queue;

	if (WARN_ON(!q))
		return;

	/* Return early if disk->queue was never registered. */
	if (!blk_queue_registered(q))
		return;

	/*
	 * Since sysfs_remove_dir() prevents adding new directory entries
	 * before removal of existing entries starts, protect against
	 * concurrent elv_iosched_store() calls.
	 */
	mutex_lock(&q->sysfs_lock);
	blk_queue_flag_clear(QUEUE_FLAG_REGISTERED, q);
	mutex_unlock(&q->sysfs_lock);

	mutex_lock(&q->sysfs_dir_lock);
	/*
	 * Remove the sysfs attributes before unregistering the queue data
	 * structures that can be modified through sysfs.
	 */
	if (queue_is_mq(q))
		blk_mq_sysfs_unregister(disk);
	blk_crypto_sysfs_unregister(disk);

	mutex_lock(&q->sysfs_lock);
	elv_unregister_queue(q);
	disk_unregister_independent_access_ranges(disk);
	mutex_unlock(&q->sysfs_lock);

	/* Now that we've deleted all child objects, we can delete the queue. */
	kobject_uevent(&disk->queue_kobj, KOBJ_REMOVE);
	kobject_del(&disk->queue_kobj);
	mutex_unlock(&q->sysfs_dir_lock);

	blk_debugfs_remove(disk);
}
