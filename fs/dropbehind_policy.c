// SPDX-License-Identifier: GPL-2.0
#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/debugfs.h>
#include <linux/dropbehind_policy.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/namei.h>
#include <linux/pagemap.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/uidgid.h>

#define DROPBEHIND_POLICY_MAX_RULES 64
#define DROPBEHIND_POLICY_MAX_PATH 512
#define DROPBEHIND_POLICY_DEFAULT_KEEP_TAIL (128U * 1024U)
#define DROPBEHIND_POLICY_DEFAULT_BATCH_BYTES (256U * 1024U)

struct dropbehind_policy_rule {
	u32 id;
	kuid_t uid;
	u64 min_size;
	u64 max_size;
	u32 keep_tail;
	u32 batch_bytes;
	u32 flags;
	atomic64_t open_matches;
	atomic64_t enabled;
	atomic64_t drop_bytes;
	atomic64_t final_drop_bytes;
	char path[DROPBEHIND_POLICY_MAX_PATH];
};

static DEFINE_MUTEX(dropbehind_policy_lock);
static struct dropbehind_policy_rule dropbehind_rules[DROPBEHIND_POLICY_MAX_RULES];
static u32 dropbehind_rule_count;
static struct dentry *dropbehind_policy_dir;

bool dropbehind_policy_has_rules(void)
{
	return READ_ONCE(dropbehind_rule_count) != 0;
}

static void dropbehind_policy_clear_locked(void)
{
	memset(dropbehind_rules, 0, sizeof(dropbehind_rules));
	WRITE_ONCE(dropbehind_rule_count, 0);
}

static bool __maybe_unused
dropbehind_policy_path_matches(const char *path,
			       const struct dropbehind_policy_rule *rule)
{
	return strcmp(path, rule->path) == 0;
}

static int dropbehind_policy_rules_show(struct seq_file *m, void *v)
{
	u32 i;

	mutex_lock(&dropbehind_policy_lock);
	seq_printf(m, "count=%u\n", dropbehind_rule_count);
	for (i = 0; i < dropbehind_rule_count; i++) {
		struct dropbehind_policy_rule *rule = &dropbehind_rules[i];

		seq_printf(m,
			   "id=%u uid=%u min_size=%llu max_size=%llu keep_tail=%u batch_bytes=%u flags=0x%x open_matches=%lld enabled=%lld drop_bytes=%lld final_drop_bytes=%lld path=%s\n",
			   rule->id, __kuid_val(rule->uid),
			   rule->min_size, rule->max_size,
			   rule->keep_tail, rule->batch_bytes, rule->flags,
			   atomic64_read(&rule->open_matches),
			   atomic64_read(&rule->enabled),
			   atomic64_read(&rule->drop_bytes),
			   atomic64_read(&rule->final_drop_bytes),
			   rule->path);
	}
	mutex_unlock(&dropbehind_policy_lock);
	return 0;
}

static int dropbehind_policy_parse_rule(char *line,
					struct dropbehind_policy_rule *rule,
					u32 id)
{
	unsigned int uid;
	unsigned int keep_tail = DROPBEHIND_POLICY_DEFAULT_KEEP_TAIL;
	unsigned int batch_bytes = DROPBEHIND_POLICY_DEFAULT_BATCH_BYTES;
	unsigned int flags = DROPBEHIND_POLICY_F_FINAL_DROP;
	unsigned long long min_size;
	unsigned long long max_size = 0;
	char path[DROPBEHIND_POLICY_MAX_PATH];
	int matched;

	matched = sscanf(line,
			 "uid=%u min_size=%llu max_size=%llu keep_tail=%u batch_bytes=%u flags=%x path=%511s",
			 &uid, &min_size, &max_size, &keep_tail, &batch_bytes,
			 &flags, path);
	if (matched != 7) {
		max_size = 0;
		keep_tail = DROPBEHIND_POLICY_DEFAULT_KEEP_TAIL;
		batch_bytes = DROPBEHIND_POLICY_DEFAULT_BATCH_BYTES;
		flags = DROPBEHIND_POLICY_F_FINAL_DROP;
		matched = sscanf(line, "uid=%u min_size=%llu path=%511s",
				 &uid, &min_size, path);
		if (matched != 3)
			return -EINVAL;
	}

	if (path[0] != '/')
		return -EINVAL;
	if (!min_size)
		return -EINVAL;
	if (!batch_bytes)
		return -EINVAL;

	memset(rule, 0, sizeof(*rule));
	rule->id = id;
	rule->uid = KUIDT_INIT(uid);
	rule->min_size = min_size;
	rule->max_size = max_size;
	rule->keep_tail = keep_tail;
	rule->batch_bytes = batch_bytes;
	rule->flags = flags;
	strscpy(rule->path, path, sizeof(rule->path));
	return 0;
}

static ssize_t dropbehind_policy_rules_write(struct file *file,
					     const char __user *ubuf,
					     size_t len, loff_t *ppos)
{
	struct dropbehind_policy_rule *parsed = NULL;
	char *buf, *line, *cursor;
	u32 count = 0;
	int ret = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (!len || len > PAGE_SIZE)
		return -EINVAL;

	buf = memdup_user_nul(ubuf, len);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	cursor = buf;
	line = strsep(&cursor, "\n");
	if (!line) {
		ret = -EINVAL;
		goto out;
	}

	if (!strcmp(line, "clear")) {
		mutex_lock(&dropbehind_policy_lock);
		dropbehind_policy_clear_locked();
		mutex_unlock(&dropbehind_policy_lock);
		goto out;
	}

	if (strcmp(line, "replace")) {
		ret = -EINVAL;
		goto out;
	}

	parsed = kcalloc(DROPBEHIND_POLICY_MAX_RULES, sizeof(*parsed),
			 GFP_KERNEL);
	if (!parsed) {
		ret = -ENOMEM;
		goto out;
	}

	while ((line = strsep(&cursor, "\n")) != NULL) {
		if (!line[0])
			continue;
		if (count >= DROPBEHIND_POLICY_MAX_RULES) {
			ret = -E2BIG;
			goto out;
		}
		ret = dropbehind_policy_parse_rule(line, &parsed[count], count + 1);
		if (ret)
			goto out;
		count++;
	}

	mutex_lock(&dropbehind_policy_lock);
	dropbehind_policy_clear_locked();
	memcpy(dropbehind_rules, parsed, sizeof(dropbehind_rules));
	WRITE_ONCE(dropbehind_rule_count, count);
	mutex_unlock(&dropbehind_policy_lock);

out:
	kfree(parsed);
	kfree(buf);
	return ret ? ret : len;
}

static int dropbehind_policy_rules_open(struct inode *inode, struct file *file)
{
	return single_open(file, dropbehind_policy_rules_show, NULL);
}

static const struct file_operations dropbehind_policy_rules_fops = {
	.open = dropbehind_policy_rules_open,
	.read = seq_read,
	.write = dropbehind_policy_rules_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static bool dropbehind_policy_size_matches(const struct dropbehind_policy_rule *rule,
					   loff_t size)
{
	if (size < rule->min_size)
		return false;
	if (rule->max_size && size > rule->max_size)
		return false;
	return true;
}

static void dropbehind_policy_apply_rule(struct file *file,
					 struct dropbehind_policy_rule *rule)
{
	u32 state_flags = 0;

	if (rule->flags & DROPBEHIND_POLICY_F_FINAL_DROP)
		state_flags |= FILE_DROPBEHIND_FINAL_DROP;

	if (!file_dropbehind_enable(file, rule->keep_tail, rule->batch_bytes,
				    state_flags, rule->id)) {
		atomic64_inc(&rule->enabled);
		atomic64_inc(&rule->open_matches);
	}
}

void dropbehind_policy_maybe_enable(struct file *file)
{
	struct inode *inode;
	char *buf, *path;
	kuid_t uid = current_uid();
	loff_t size;
	u32 count, i;

	count = READ_ONCE(dropbehind_rule_count);
	if (!count)
		return;

	inode = file_inode(file);
	if (!S_ISREG(inode->i_mode))
		return;
	if (file->f_mode & FMODE_WRITE)
		return;
	if (!(file->f_op->fop_flags & FOP_DONTCACHE))
		return;
	if (IS_DAX(inode))
		return;

	size = i_size_read(inode);
	buf = __getname();
	if (!buf)
		return;

	path = file_path(file, buf, PATH_MAX);
	if (IS_ERR(path))
		goto out;

	mutex_lock(&dropbehind_policy_lock);
	count = dropbehind_rule_count;
	for (i = 0; i < count; i++) {
		struct dropbehind_policy_rule *rule = &dropbehind_rules[i];

		if (!uid_eq(uid, rule->uid))
			continue;
		if (!dropbehind_policy_size_matches(rule, size))
			continue;
		if (dropbehind_policy_path_matches(path, rule)) {
			dropbehind_policy_apply_rule(file, rule);
			break;
		}
	}
	mutex_unlock(&dropbehind_policy_lock);

out:
	__putname(buf);
}

void dropbehind_policy_account_drop(struct file *file, u64 bytes, bool final)
{
	u32 id = READ_ONCE(file->f_dropbehind_policy_rule_id);
	struct dropbehind_policy_rule *rule;

	if (!id || !bytes)
		return;

	mutex_lock(&dropbehind_policy_lock);
	if (id > dropbehind_rule_count)
		goto out;
	rule = &dropbehind_rules[id - 1];
	if (rule->id != id)
		goto out;

	atomic64_add(bytes, &rule->drop_bytes);
	if (final)
		atomic64_add(bytes, &rule->final_drop_bytes);
out:
	mutex_unlock(&dropbehind_policy_lock);
}

static int __init dropbehind_policy_init(void)
{
	dropbehind_policy_dir = debugfs_create_dir("dropbehind_policy", NULL);
	debugfs_create_file("rules", 0600, dropbehind_policy_dir, NULL,
			    &dropbehind_policy_rules_fops);
	return 0;
}
fs_initcall(dropbehind_policy_init);
