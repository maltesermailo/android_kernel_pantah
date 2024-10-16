// SPDX-License-Identifier: GPL-2.0
/*
 * Non-trivial C macros cannot be used in Rust. Similarly, inlined C functions
 * cannot be called either. This file explicitly creates functions ("helpers")
 * that wrap those so that they can be called from Rust.
 *
 * Sorted alphabetically.
 */

#include "blk.c"
#include "bug.c"
#include "build_assert.c"
#include "build_bug.c"
<<<<<<< HEAD   (d31999 ANDROID: rust_binder: add binder_logs/proc directory)
#include "cred.c"
#include "err.c"
#include "fs.c"
#include "kunit.c"
#include "list_lru.c"
#include "mm.c"
#include "mutex.c"
#include "page.c"
#include "rbtree.c"
#include "refcount.c"
#include "security.c"
#include "signal.c"
#include "slab.c"
#include "spinlock.c"
#include "task.c"
#include "task_work.c"
||||||| BASE
=======
#include "err.c"
#include "kunit.c"
#include "mutex.c"
#include "page.c"
#include "rbtree.c"
#include "refcount.c"
#include "signal.c"
#include "slab.c"
#include "spinlock.c"
#include "task.c"
>>>>>>> BRANCH (6b14bc Merge 9852d85ec9d4 ("Linux 6.12-rc1") into android-mainline)
#include "uaccess.c"
#include "wait.c"
#include "workqueue.c"
