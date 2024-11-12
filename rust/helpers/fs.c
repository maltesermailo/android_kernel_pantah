// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2024 Google LLC.
 */

#include <linux/fs.h>

struct file *rust_helper_get_file(struct file *f)
{
	return get_file(f);
}

int rust_helper_mapping_writably_mapped(struct address_space *mapping)
{
	return mapping_writably_mapped(mapping);
}
