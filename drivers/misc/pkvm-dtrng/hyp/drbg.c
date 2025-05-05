// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 - Google Inc
 */

#include "drbg.h"
#include "dtrng-common.h"

static inline void hmac_pad_key(const u8 *key, size_t key_size, u8 *ipad, u8 *opad,
				size_t pad_size)
{
	size_t i;
	u8 b;

	for (i = 0; i < pad_size; i++) {
		b = i < key_size ? key[i] : 0;

		ipad[i] = b ^ 0x5c;
		opad[i] = b ^ 0x36;
	}
}

void hmac_sha256_init(struct hmac_sha256 *hmac, const u8 *key, size_t key_size)
{
	u8 padded_key[SHA256_DIGEST_SIZE];
	u8 ipad_key[SHA256_BLOCK_SIZE];

	if (key_size > SHA256_BLOCK_SIZE) {
		sha256(key, key_size, padded_key);
		hmac_pad_key(padded_key, sizeof(padded_key), ipad_key,
			     hmac->opad_key, sizeof(ipad_key));
	} else {
		hmac_pad_key(key, key_size, ipad_key, hmac->opad_key,
			     sizeof(ipad_key));
	}

	sha256_init(&hmac->digest_state);
	sha256_update(&hmac->digest_state, ipad_key, sizeof(ipad_key));
}

void hmac_sha256_update(struct hmac_sha256 *hmac, const u8 *data, size_t data_size)
{
	sha256_update(&hmac->digest_state, data, data_size);
}

void hmac_sha256_finish(struct hmac_sha256 *hmac, u8 *out)
{
	u8 ihash[SHA256_DIGEST_SIZE];
	struct sha256_state ohash;

	sha256_final(&hmac->digest_state, ihash);

	sha256_init(&ohash);
	sha256_update(&ohash, hmac->opad_key, sizeof(hmac->opad_key));
	sha256_update(&ohash, ihash, sizeof(ihash));
	sha256_final(&ohash, out);
}

int drbg256_init(struct drbg256_generator *gen)
{
	gen->req_counter = 0;
	return 0;
}
