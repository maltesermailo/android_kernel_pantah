/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM cgroup
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_CGROUP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_CGROUP_H
#include <trace/hooks/vendor_hooks.h>

struct cgroup_taskset;
struct cgroup_subsys;
struct cgroup_subsys_state;
<<<<<<< HEAD   (92006aa7394b93146726ae6bdcfd782233f202dc ANDROID: GKI: Update qcom symbol list for SDEI)
||||||| BASE   (3b58573735e1d2f2c3eff856ed4c596f4d52a61d ANDROID: mm: create vendor hooks for page alloc)
DECLARE_HOOK(android_vh_cgroup_set_task,
	TP_PROTO(int ret, struct task_struct *task),
	TP_ARGS(ret, task));

=======
DECLARE_HOOK(android_vh_cgroup_set_task,
	TP_PROTO(int ret, struct cgroup *cgrp, struct task_struct *task, bool threadgroup),
	TP_ARGS(ret, cgrp, task, threadgroup));

>>>>>>> CHANGE (578bcc20c07e88c093e63192c838b7245b3bfc01 ANDROID: cgroup: Add trace_android_vh_cgroup_set_task parame)
DECLARE_HOOK(android_vh_cgroup_attach,
	TP_PROTO(struct cgroup_subsys *ss, struct cgroup_taskset *tset),
	TP_ARGS(ss, tset));

DECLARE_RESTRICTED_HOOK(android_rvh_cpu_cgroup_attach,
	TP_PROTO(struct cgroup_taskset *tset),
	TP_ARGS(tset), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_cpu_cgroup_online,
	TP_PROTO(struct cgroup_subsys_state *css),
	TP_ARGS(css), 1);
#endif

#include <trace/define_trace.h>
