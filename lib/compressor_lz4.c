// SPDX-License-Identifier: GPL-2.0+
/*
 * erofs-utils/lib/compressor-lz4.c
 *
 * Copyright (C) 2018-2019 HUAWEI, Inc.
 *             http://www.huawei.com/
 * Created by Gao Xiang <gaoxiang25@huawei.com>
 */
#include <lz4.h>
#include "erofs/internal.h"
#include "compressor.h"

#define MINMATCH 4
#define WILDCOPYLENGTH 8

/*
 * ensure it's possible to write 2 x wildcopyLength
 * without overflowing output buffer
 */
#define MATCH_SAFEGUARD_DISTANCE  ((2 * WILDCOPYLENGTH) - MINMATCH)


#define ML_BITS  4
#define ML_MASK  ((1U<<ML_BITS)-1)
#define RUN_BITS (8-ML_BITS)
#define RUN_MASK ((1U<<RUN_BITS)-1)

/*
 * works well for lz4 only <= 1.8.3 (without LZ4_FAST_DEC_LOOP)
 * TODO: support LZ4_FAST_DEC_LOOP in order to decompress in-place on lz4 1.9.x
 */
static bool lz4_try_decompress_inplace(struct erofs_compress *c,
				       const void *src, unsigned int srcsize,
				       const void *dst, unsigned int dstsize)
{
	const u8 *ip = src;
	const u8 *iend = src + srcsize;
	const u8 *op = dst;
	const u8 *oend = dst + dstsize;

	/* Set up the "end" pointers for the shortcut. */
	const u8* const shortiend = iend - 14 /*maxLL*/ - 2 /*offset*/;
	const u8* const shortoend = oend - 14 /*maxLL*/ - 18 /*maxML*/;

	while(ip < iend) {
		/* 1. process token */
		unsigned int literals = *ip >> 4;
		unsigned int matchlength = *ip & 15;
		bool shortcut;

		++ip;
		if (literals == 15) {
			do {
				literals += *ip;
			} while (*ip++ == 255);
		}

		ip += literals;
		/* EOF - dont care the last literal */
		if (ip >= iend)
			break;
		op += literals;

		shortcut = false;
		if (literals < RUN_MASK &&
		    /* strictly "less than" on input, to re-enter the loop with at least one byte */
		    ((ip < shortiend) & (op <= shortoend))) {
			if (op + 16 > ip)
				return false;
			shortcut = true;
		} else if (op <= oend - WILDCOPYLENGTH) {
			if (op + 8 > ip)
				return false;
		}

		/* read offset */
		ip += 2;

		if (matchlength == 15) {
			do {
				matchlength += *ip;
			} while (*ip++ == 255);
		}

		matchlength += 4;
		op += matchlength;

		if (shortcut && matchlength < 19) {
			if (op + 18 > ip)
				return false;
		} else if (op <= oend - MATCH_SAFEGUARD_DISTANCE) {
			/* due to LZ4_wildCopy8 * 2 */
			if (op + 16 > ip)
				return false;
		} else if (op <= oend - WILDCOPYLENGTH) {
			if (op + 8 > ip)
				return false;
		}
	}
	return true;
}

bool lz4_can_decompress_inplace(struct erofs_compress *c,
				const void *src, unsigned int srcsize,
				const void *dst, unsigned int dstsize)
{
	/* TODO: add the reason why? */
	if (dst + dstsize + 18 < src + srcsize - ((srcsize - 14) / 15)) {
		DBG_BUGON(!lz4_try_decompress_inplace(c, src, srcsize,
						      dst, dstsize));
		return true;
	}
	return lz4_try_decompress_inplace(c, src, srcsize, dst, dstsize);
}

static int lz4_compress_destsize(struct erofs_compress *c,
				 int compression_level,
				 void *src, unsigned int *srcsize,
				 void *dst, unsigned int dstsize)
{
	int srcSize = (int)*srcsize;
	int rc = LZ4_compress_destSize(src, dst, &srcSize, (int)dstsize);

	if (!rc)
		return -EFAULT;
	*srcsize = srcSize;
	return rc;
}

static int compressor_lz4_exit(struct erofs_compress *c)
{
	return 0;
}

static int compressor_lz4_init(struct erofs_compress *c,
				 char *alg_name)
{
	if (alg_name && strcmp(alg_name, "lz4"))
		return -EINVAL;
	c->alg = &erofs_compressor_lz4;
	return 0;
}

struct erofs_compressor erofs_compressor_lz4 = {
	.default_level = 0,
	.best_level = 0,
	.init = compressor_lz4_init,
	.exit = compressor_lz4_exit,
	.compress_destsize = lz4_compress_destsize,
	.can_decompress_inplace = lz4_can_decompress_inplace,
};

