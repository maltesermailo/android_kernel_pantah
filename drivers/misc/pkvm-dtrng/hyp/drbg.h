// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 - Google Inc
 */

#ifndef __DTRNG_DRBG_H__
#define __DTRNG_DRBG_H__

#include <crypto/sha256_base.h>

#define HMAC_PAD_KEY_SIZE 64

struct hmac_sha256 {
	u8 opad_key[SHA256_BLOCK_SIZE];
	struct sha256_state digest_state;
};

void hmac_sha256_init(struct hmac_sha256 *hmac, const u8 *key, size_t key_size);

void hmac_sha256_update(struct hmac_sha256 *hmac, const u8 *data, size_t data_size);

void hmac_sha256_finish(struct hmac_sha256 *hmac, u8 *out);

static inline void hmac_sha256(u8 *key, size_t key_size, u8 *data,
			       size_t data_size, u8 *out)
{
	struct hmac_sha256 hmac;

	hmac_sha256_init(&hmac, key, key_size);
	hmac_sha256_update(&hmac, data, data_size);
	hmac_sha256_finish(&hmac, out);
}

struct drbg256_generator {
	u8 v[SHA256_DIGEST_SIZE];
	u8 key[SHA256_DIGEST_SIZE];
	u64 req_counter;
};

int drbg256_init(struct drbg256_generator *gen);

#endif
