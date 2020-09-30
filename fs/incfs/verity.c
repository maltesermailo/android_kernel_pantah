// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Google LLC
 */

#include <linux/fsverity.h>

#include "data_mgmt.h"
#include "format.h"
#include "integrity.h"
#include "vfs.h"

static int incfs_begin_enable_verity(struct file *filp)
{
	return 0;
}

static int incfs_end_enable_verity(struct file *filp, const void *desc,
				   size_t desc_size, u64 merkle_tree_size)
{
	struct inode *inode = file_inode(filp);
	struct mem_range descriptor = {
		.data = (void *) desc,
		.len = desc_size,
	};
	struct data_file *df = get_incfs_data_file(filp);
	struct backing_file_context *bfc;
	int error;
	struct incfs_df_verity_descriptor *vd;
	loff_t offset;

	if (!desc)
		return 0;

	if (!df || !df->df_backing_file_context)
		return -EFAULT;

	bfc = df->df_backing_file_context;
	error = mutex_lock_interruptible(&bfc->bc_mutex);
	if (error)
		return error;
	error = incfs_write_verity_descriptor_to_backing_file(bfc, descriptor,
							       &offset);
	mutex_unlock(&bfc->bc_mutex);
	if (error)
		return error;

	vd = kzalloc(sizeof(*vd), GFP_NOFS);
	if (!vd)
		return -ENOMEM;

	*vd = (struct incfs_df_verity_descriptor) {
		.size = desc_size,
		.offset = offset,
	};

	df->df_verity_descriptor = vd;
	inode->i_private = df;
	inode_set_flags(inode, S_VERITY, S_VERITY);

	return 0;
}

static int incfs_get_verity_descriptor(struct inode *inode, void *buf,
				       size_t buf_size)
{
	struct data_file *df = inode->i_private;
	struct incfs_df_verity_descriptor *vd;
	int read;

	if (!df || !df->df_backing_file_context)
		return -EFAULT;

	vd = df->df_verity_descriptor;
	if (!vd)
		return 0;

	if (!buf_size)
		return vd->size;

	if (vd->size > buf_size)
		return -ERANGE;

	read = incfs_kread(df->df_backing_file_context->bc_file, buf, vd->size,
		    vd->offset);

	if (read < 0)
		return read;

	if (read != vd->size)
		return -EINVAL;

	return read;
}

static int incfs_get_root_hash(struct file *filp,
			       const struct fsverity_enable_arg *arg,
			       u8 *root_hash)
{
	struct data_file *df = get_incfs_data_file(filp);
	struct mtree *hash_tree;

	if (!df)
		return -EINVAL;

	if (arg->hash_algorithm != FS_VERITY_HASH_ALG_SHA256 ||
	    arg->block_size != INCFS_DATA_FILE_BLOCK_SIZE ||
	    arg->salt_size != 0)
		return -EINVAL;
	/*
	 * Memory barrier to ensure hash tree fully present if being added via
	 * enable verity
	 */
	hash_tree = smp_load_acquire(&df->df_hash_tree);
	if (!hash_tree)
		return -EOPNOTSUPP;

	memcpy(root_hash, df->df_hash_tree->root_hash,
	       df->df_hash_tree->alg->digest_size);

	return 0;
}

int incfs_verity_get_flags(struct file *f, void __user *arg)
{
	u32 flags = (file_inode(f)->i_flags & S_VERITY) ? FS_VERITY_FL : 0;

	return put_user(flags, (int __user *) arg);
}

const struct fsverity_operations incfs_verityops = {
	.begin_enable_verity		= incfs_begin_enable_verity,
	.end_enable_verity		= incfs_end_enable_verity,
	.get_verity_descriptor		= incfs_get_verity_descriptor,
	.get_root_hash			= incfs_get_root_hash,
};

static int build_merkle_tree(struct file *f, struct data_file *df,
			     struct backing_file_context *bfc,
			     struct mtree *hash_tree, loff_t hash_offset,
			     struct incfs_hash_alg *alg, struct mem_range hash)
{
	int error = 0;
	struct mem_range buf = {.len = INCFS_DATA_FILE_BLOCK_SIZE};
	struct mem_range tmp = {.len = 2 * INCFS_DATA_FILE_BLOCK_SIZE};
	int limit, lvl, i, result;

	buf.data = (u8 *)__get_free_pages(GFP_NOFS, get_order(buf.len));
	tmp.data = (u8 *)__get_free_pages(GFP_NOFS, get_order(tmp.len));
	if (!buf.data || !tmp.data) {
		error = -ENOMEM;
		goto out;
	}

	/*
	 * lvl - 1 is the level we are reading, lvl the level we are writing
	 * lvl == -1 means actual blocks
	 * lvl == hash_tree->depth means root hash
	 */
	limit = df->df_data_block_count;
	for (lvl = 0; lvl <= hash_tree->depth; lvl++) {
		for (i = 0; i < limit; ++i) {
			loff_t hash_level;

			buf.len = INCFS_DATA_FILE_BLOCK_SIZE;
			if (lvl == 0)
				result = incfs_read_data_file_block(buf, f,
							    i, 0, 0, 0, tmp);
			else {
				hash_level = hash_offset +
				       hash_tree->hash_level_suboffset[lvl - 1];

				result = incfs_kread(bfc->bc_file, buf.data,
						buf.len, hash_level + i *
						    INCFS_DATA_FILE_BLOCK_SIZE);
			}

			if (result < 0) {
				error = result;
				goto out;
			}

			buf.len = result;
			error = incfs_calc_digest(alg, buf, hash);
			if (error)
				goto out;

			/*
			 * last level - only one hash to take and it is stored
			 * in the signature
			 */
			if (lvl == hash_tree->depth)
				break;

			hash_level = hash_offset +
				hash_tree->hash_level_suboffset[lvl];

			result = incfs_kwrite(bfc->bc_file, hash.data, hash.len,
					hash_level + hash.len * i);

			if (result < 0) {
				error = result;
				goto out;
			}

			if (result != hash.len) {
				error = -EIO;
				goto out;
			}
		}
		limit = DIV_ROUND_UP(limit,
				     INCFS_DATA_FILE_BLOCK_SIZE / hash.len);
	}

out:
	free_pages((unsigned long)tmp.data, get_order(tmp.len));
	free_pages((unsigned long)buf.data, get_order(buf.len));
	return error;
}

static int sign_file(struct file *f, struct data_file *df)
{
	/* See incfs_parse_signature */
	struct {
		u32 version;
		u32 size_of_hash_info_section;
		struct {
			u32 hash_algorithm;
			u8 log2_blocksize;
			u32 salt_size;
			u8 salt[0];
			u32 hash_size;
			u8 root_hash[32];
		} __packed hash_section;
		u32 size_of_signing_info_section;
		u8 signing_info_section[0];
	} __packed sig = {
		.version = INCFS_SIGNATURE_VERSION,
		.size_of_hash_info_section = sizeof(sig.hash_section),
		.hash_section = {
			.hash_algorithm = INCFS_HASH_TREE_SHA256,
			.log2_blocksize = 12,
			.hash_size = 32,
		},
	};

	struct mtree *hash_tree = NULL;
	struct backing_file_context *bfc;
	int error;
	loff_t hash_offset, sig_offset;
	struct incfs_hash_alg *alg = incfs_get_hash_alg(INCFS_HASH_TREE_SHA256);
	u8 hash_buf[INCFS_MAX_HASH_SIZE];
	int hash_size = alg->digest_size;
	struct mem_range hash = range(hash_buf, hash_size);
	int result;
	struct incfs_df_signature *signature = NULL;

	if (df->df_signature || df->df_hash_tree)
		return -EFAULT;

	/* Add signature metadata record to file */
	hash_tree = incfs_alloc_mtree(range((u8 *)&sig, sizeof(sig)),
				      df->df_data_block_count);
	if (IS_ERR(hash_tree))
		return PTR_ERR(hash_tree);

	bfc = df->df_backing_file_context;
	if (!bfc)
		goto out;

	error = mutex_lock_interruptible(&bfc->bc_mutex);
	if (error)
		goto out;

	error = incfs_write_signature_to_backing_file(bfc,
				range((u8 *)&sig, sizeof(sig)),
				hash_tree->hash_tree_area_size,
				&hash_offset, &sig_offset);
	mutex_unlock(&bfc->bc_mutex);

	/* Populate merkle tree */
	error = build_merkle_tree(f, df, bfc, hash_tree, hash_offset, alg,
				  hash);
	if (error)
		goto out;

	/* Update signature metadata record */
	memcpy(sig.hash_section.root_hash, hash.data, alg->digest_size);
	result = incfs_kwrite(bfc->bc_file, &sig, sizeof(sig), sig_offset);
	if (result < 0) {
		error = result;
		goto out;
	}

	if (result != sizeof(sig)) {
		error = -EIO;
		goto out;
	}

	/* Update in-memory records */
	memcpy(hash_tree->root_hash, hash.data, alg->digest_size);
	signature = kzalloc(sizeof(*signature), GFP_NOFS);
	if (!signature) {
		error = -ENOMEM;
		goto out;
	}
	*signature = (struct incfs_df_signature) {
		.hash_offset = hash_offset,
		.hash_size = hash_tree->hash_tree_area_size,
		.sig_offset = sig_offset,
		.sig_size = sizeof(sig),
	};
	df->df_signature = signature;
	signature = NULL;

	/*
	 * Use memory barrier to prevent readpage seeing the hash tree until
	 * it's fully there
	 */
	smp_store_release(&df->df_hash_tree, hash_tree);
	hash_tree = NULL;

out:
	kfree(signature);
	kfree(hash_tree);
	return error;
}

long incfs_fsverity_enable(struct file *f, const void __user *arg)
{
	int error;
	struct data_file *df = get_incfs_data_file(f);

	if (!df)
		return -EINVAL;

	if (df->df_header_flags & INCFS_FILE_MAPPED)
		return -EINVAL;

	error = mutex_lock_interruptible(&df->df_enable_verity);
	if (error)
		return error;

	if (!df->df_signature || !df->df_hash_tree)
		error = sign_file(f, df);

	mutex_unlock(&df->df_enable_verity);
	if (error)
		return error;

	return fsverity_ioctl_enable(f, (const void __user *)arg);
}

