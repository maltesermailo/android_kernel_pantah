// SPDX-License-Identifier: GPL-2.0

#include <linux/export.h>
#include <linux/spinlock.h>

void rust_helper___spin_lock_init(spinlock_t *lock, const char *name,
				  struct lock_class_key *key)
{
#ifdef CONFIG_DEBUG_SPINLOCK
	__raw_spin_lock_init(spinlock_check(lock), name, key, LD_WAIT_CONFIG);
#else
	spin_lock_init(lock);
#endif
}

void rust_helper_spin_lock(spinlock_t *lock)
{
	spin_lock(lock);
}

void rust_helper_spin_unlock(spinlock_t *lock)
{
	spin_unlock(lock);
}
<<<<<<< HEAD   (d31999 ANDROID: rust_binder: add binder_logs/proc directory)

int rust_helper_spin_trylock(spinlock_t *lock)
{
	return spin_trylock(lock);
}
||||||| BASE
=======
>>>>>>> BRANCH (6b14bc Merge 9852d85ec9d4 ("Linux 6.12-rc1") into android-mainline)
