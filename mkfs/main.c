// SPDX-License-Identifier: GPL-2.0+
/*
 * mkfs/main.c
 *
 * Copyright (C) 2018-2019 HUAWEI, Inc.
 *             http://www.huawei.com/
 * Created by Li Guifu <bluce.liguifu@huawei.com>
 */
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <limits.h>
#include <libgen.h>
#include "erofs/config.h"
#include "erofs/print.h"
#include "erofs/cache.h"
#include "erofs/inode.h"
#include "erofs/io.h"
#include "erofs/compress.h"

#define EROFS_SUPER_END (EROFS_SUPER_OFFSET + sizeof(struct erofs_super_block))

static void usage(char *execpath)
{
	fprintf(stderr, "%s %s\n", basename(execpath), cfg.c_version);
	fprintf(stderr, "\nUsage:\n");
	fprintf(stderr, "    [-z <compr_algri>] [-d <dbglvl>]\n");
	fprintf(stderr, "    [target path] [source directory]\n");
}

u64 parse_num_from_str(const char *str)
{
	u64 num      = 0;
	char *endptr = NULL;

	num = strtoull(str, &endptr, 10);
	BUG_ON(num == ULLONG_MAX);
	return num;
}

static int mkfs_parse_options_cfg(int argc, char *argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "d:z:")) != -1) {
		switch (opt) {
		case 'z':
			cfg.c_compr_alg_master = optarg ?
				strdup(optarg) : "(default)";
			break;

		case 'd':
			cfg.c_dbg_lvl = parse_num_from_str(optarg);
			break;

		default: /* '?' */
			return -EINVAL;
		}
	}

	if (optind >= argc)
		return -EINVAL;

	cfg.c_img_path = strdup(argv[optind++]);
	if (!cfg.c_img_path)
		return -ENOMEM;

	if (optind > argc) {
		erofs_err("Source directory is missing");
		return -EINVAL;
	}

	cfg.c_src_path = realpath(argv[optind++], NULL);
	if (!cfg.c_src_path) {
		erofs_err("Failed to parse source directory: %s",
			  erofs_strerror(-errno));
		return -ENOENT;
	}

	if (optind < argc) {
		erofs_err("Unexpected argument: %s\n", argv[optind]);
		return -EINVAL;
	}
	return 0;
}

int erofs_mkfs_update_super_block(struct erofs_buffer_head *bh,
				  erofs_nid_t root_nid)
{
	struct erofs_super_block sb = {
		.magic     = cpu_to_le32(EROFS_SUPER_MAGIC_V1),
		.blkszbits = LOG_BLOCK_SIZE,
		.inos   = 0,
		.blocks = 0,
		.meta_blkaddr  = sbi.meta_blkaddr,
		.xattr_blkaddr = 0,
	};
	const unsigned int sb_blksize =
		round_up(EROFS_SUPER_END, EROFS_BLKSIZ);
	char *buf;
	struct timeval t;

	if (!gettimeofday(&t, NULL)) {
		sb.build_time      = cpu_to_le64(t.tv_sec);
		sb.build_time_nsec = cpu_to_le32(t.tv_usec);
	}

	sb.blocks       = cpu_to_le32(erofs_mapbh(NULL, true));
	sb.root_nid     = cpu_to_le16(root_nid);

	buf = calloc(sb_blksize, 1);
	if (!buf) {
		erofs_err("Failed to allocate memory for sb: %s",
			  erofs_strerror(-errno));
		return -ENOMEM;
	}
	memcpy(buf + EROFS_SUPER_OFFSET, &sb, sizeof(sb));

	bh->fsprivate = buf;
	bh->op = &erofs_buf_write_bhops;
	return 0;
}

int main(int argc, char **argv)
{
	int err = 0;
	struct erofs_buffer_head *sb_bh;
	struct erofs_inode *root_inode;
	erofs_nid_t root_nid;

	erofs_init_configure();
	err = mkfs_parse_options_cfg(argc, argv);
	if (err) {
		if (err == -EINVAL)
			usage(argv[0]);
		return 1;
	}

	err = dev_open(cfg.c_img_path);
	if (err) {
		usage(argv[0]);
		return 1;
	}

	erofs_err("%s %s\n", basename(argv[0]), cfg.c_version);
	erofs_show_config();

	sb_bh = erofs_buffer_init();
	err = erofs_bh_balloon(sb_bh, EROFS_SUPER_END);
	if (err < 0) {
		erofs_err("Failed to balloon erofs_super_block: %s",
			  erofs_strerror(err));
		goto exit;
	}

	z_erofs_compress_init();
	erofs_inode_manager_init();

	root_inode = erofs_mkfs_build_tree_from_path(NULL, cfg.c_src_path);
	if (IS_ERR(root_inode)) {
		err = PTR_ERR(root_inode);
		goto exit;
	}

	root_nid = erofs_lookupnid(root_inode);
	erofs_iput(root_inode);

	err = erofs_mkfs_update_super_block(sb_bh, root_nid);
	if (err)
		goto exit;

	/* flush all remaining buffers */
	if (!erofs_bflush(NULL))
		err = -EIO;
exit:
	z_erofs_compress_exit();
	dev_close();
	erofs_exit_configure();

	if (err) {
		erofs_err("\tCould not format the device : %s\n",
			  erofs_strerror(err));
		return 1;
	}
	return err;
}
