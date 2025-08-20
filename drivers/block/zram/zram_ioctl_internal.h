/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _ZRAM_IOCTL_H_
#define _ZRAM_IOCTL_H_

#include <uapi/linux/zram_ioctl.h>

#if IS_ENABLED(CONFIG_ZRAM_WRITEBACK)
int zram_ioctl_process_writeback_scan(struct zram *zram,
				      struct zram_ioc_data *ioc_data,
				      struct zram_pp_ctl *ctl);
#endif

#endif /* _ZRAM_IOCTL_H_ */

