/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_DROPBEHIND_POLICY_H
#define _LINUX_DROPBEHIND_POLICY_H

#include <linux/fs.h>
#include <linux/types.h>

#define DROPBEHIND_POLICY_F_FINAL_DROP		(1U << 0)

bool dropbehind_policy_has_rules(void);
void dropbehind_policy_maybe_enable(struct file *file);
void dropbehind_policy_account_drop(struct file *file, u64 bytes, bool final);

/* dropbehind_policy_init is private to fs/dropbehind_policy.c. */

#endif /* _LINUX_DROPBEHIND_POLICY_H */
