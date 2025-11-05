#ifndef _BPF_WAKEUP_SOURCE_ITER_H
#define _BPF_WAKEUP_SOURCE_ITER_H

#include <linux/bpf.h>
#include <linux/pm_wakeup.h>

struct bpf_iter_wakeup_source;

__bpf_kfunc int bpf_iter_wakeup_source_new(struct bpf_iter_wakeup_source *it);
__bpf_kfunc struct wakeup_source *bpf_iter_wakeup_source_next(struct bpf_iter_wakeup_source *it);
__bpf_kfunc void bpf_iter_wakeup_source_destroy(struct bpf_iter_wakeup_source *it);

#endif /* _BPF_WAKEUP_SOURCE_ITER_H */