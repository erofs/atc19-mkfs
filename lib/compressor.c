// SPDX-License-Identifier: GPL-2.0+
/*
 * erofs-utils/lib/compressor.c
 *
 * Copyright (C) 2018-2019 HUAWEI, Inc.
 *             http://www.huawei.com/
 * Created by Gao Xiang <gaoxiang25@huawei.com>
 */
#include "erofs/internal.h"
#include "compressor.h"

#define EROFS_CONFIG_COMPR_DEF_BOUNDARY		(128)

int erofs_compress_destsize(struct erofs_compress *c,
			    int compression_level,
			    void *src,
			    unsigned int *srcsize,
			    void *dst,
			    unsigned int dstsize)
{
	int ret;

	DBG_BUGON(!c->alg);
	if (!c->alg->compress_destsize)
		return -ENOTSUP;

	ret = c->alg->compress_destsize(c, compression_level,
					src, srcsize, dst, dstsize);
	if (ret < 0)
		return ret;

	/* check if there is enough gains to compress */
	if (*srcsize <= dstsize * c->compress_threshold / 100)
		return -EAGAIN;
	return ret;
}

int erofs_compressor_init(struct erofs_compress *c,
			  char *alg_name)
{
	static struct erofs_compressor *compressors[] = {
#if LZ4_ENABLED
#if LZ4HC_ENABLED
		&erofs_compressor_lz4hc,
#endif
		&erofs_compressor_lz4,
#endif
	};

	int ret, i;

	/* should be written in "minimum compression ratio * 100" */
	c->compress_threshold = 100;

	/* optimize for 4k size page */
	c->destsize_alignsize = PAGE_SIZE;
	c->destsize_redzone_begin = PAGE_SIZE - 16;
	c->destsize_redzone_end = EROFS_CONFIG_COMPR_DEF_BOUNDARY;

	ret = -EINVAL;

	for (i = 0; i < ARRAY_SIZE(compressors); ++i) {
		ret = compressors[i]->init(c, alg_name);
		if (!ret) {
			DBG_BUGON(!c->alg);
			return 0;
		}
	}
	return ret;
}

int erofs_compressor_exit(struct erofs_compress *c)
{
	DBG_BUGON(!c->alg);

	if (c->alg->exit)
		return c->alg->exit(c);
	return 0;
}

