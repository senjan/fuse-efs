/*
 * fuse-efs - FUSE module for SGI EFS
 * https://github.com/senjan/fuse-efs
 * Copyright (C) 2024 Jan Senolt.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>

#define	FUSE_USE_VERSION 26

#include <fuse.h>

#include <assert.h>

#include "efs_fs.h"
#include "efs_vol.h"
#include "efs_file.h"
#include "efs_dir.h"

#include "utils.h"

efs_fs_t fs = { 0 };

static struct options {
	char *fs_image;
	int log_lvl;
	int part;
	int show_help;
} options;

#define	OPTION(t, p) \
	{ t, offsetof(struct options, p), 1 }
static struct fuse_opt efs_opts[] = {
	OPTION("--fs=%s", fs_image),
	OPTION("--debug=%d", log_lvl),
	OPTION("--partition=%d", part),
	OPTION("-h", show_help),
	OPTION("--help", show_help),
	FUSE_OPT_END
};

static int
efs_statfs(const char *path, struct statvfs *st)
{
	st->f_bsize = BBS;
	st->f_frsize = BBS;
	st->f_blocks = GET_I32(fs.sb.s_size);
	st->f_bfree = GET_I32(fs.sb.s_blk_free);
	st->f_bavail = st->f_bfree;
	st->f_files = GET_I32(fs.sb.s_ino_free) * 2;
	st->f_ffree = GET_I32(fs.sb.s_ino_free);
	st->f_favail = st->f_ffree;
	st->f_fsid = 0;
#if defined(__SOLARIS__)
	(void) strncpy(st->f_basetype, EFS_NAME, FSTYPSZ);
#endif
	return (0);
}

#ifdef EFS_DEBUG
/*
 * This is an alternative and now unused implementation of the readdir method.
 */
static int
efs_readdir_alt(const char *path, void *buf, fuse_fill_dir_t filler,
    off_t offset, struct fuse_file_info *fi)
{
	efs_inode_t *inode;
	mode_t mode;
	uint32_t blkno = 0;
	int err;

	if ((err = efs_dir_namei(&fs, path, &inode)) != 0) {
		LOG_ERR("err=%d\n", err);
		return (err);
	}

	mode = inode->i_mode;
	if ((mode & S_IFMT) != S_IFDIR) {
		LOG_ERR("%s is not a dir!\n", path);
		return (ENOENT);
	}

	for (;;) {
		efs_inode_t *item_inode;
		efs_dirblk_t db;

		err = efs_iread(inode, blkno++, 1, &db);
		if (err != 0) {
			LOG_DBG1(inode->i_fs, "%s: err=%d\n", __func__, err);
			err = 0;
			break;
		}
		if (GET_U16(db.db_magic) != 0xbeef) {
			LOG_DBG1(inode->i_fs, "%s: block %u of %s has wrong "
			    "magic number 0%x", __func__, blkno - 1, path,
			    GET_U16(db.db_magic));
			err = ENXIO;
			break;
		}
		LOG_DBG1(inode->i_fs, "%s: has %d slots\n", __func__,
		    db.db_slots);
		for (int i = 0; i < db.db_slots; i++) {
			char *name;
			uint32_t ino;
			err = efs_dir_get_dirent(&db, i, &ino, &name);
			if (err != 0)
				break;
			err = efs_iget(inode->i_fs, ino, &item_inode);
			LOG_DBG1(inode->i_fs, "%s: slot %d, inode %d: '%s'\n",
			    __func__, i, ino, name);
			filler(buf, name, &item_inode->i_stat, 0);
		}
	}

	LOG_DBG1(inode->i_fs, "%s: done - blkno=%d.\n", __func__, blkno - 1);

	return (err);
}
#endif

static int
efs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
    off_t offset, struct fuse_file_info *fi)
{
	efs_inode_t *inode;
	mode_t mode;
	uint32_t blkno = offset / EFS_DIR_ENTRY_MOD;
	uint8_t slotno = offset % EFS_DIR_ENTRY_MOD;
	int err;
	int done = 0;

	LOG_DBG2(&fs, "%s: path '%s', offset %lu, blkno %u, "
	    "slotno=%u\n", __func__, path, offset, blkno, slotno);

	if ((err = efs_dir_namei(&fs, path, &inode)) != 0) {
		LOG_ERR("cannot find '%s'.\n", path);
		return (err);
	}

	mode = inode->i_mode;
	if ((mode & S_IFMT) != S_IFDIR) {
		LOG_ERR("%s is not a directory\n", path);
		return (ENOENT);
	}

	while (!done) {
		efs_inode_t *item_inode;
		efs_dirblk_t db;

		err = efs_iread(inode, blkno, 1, &db);
		if (err != 0) {
			done = 1;
			if (err == ENXIO) {
				/* Reached the end of the directory. */
				err = 0;
			} else {
				LOG_ERR("%s: iread failed for blk %d of '%s', "
				    "error: %d\n", __func__, blkno, path, err);
			}
			break;
		}
		if (GET_U16(db.db_magic) != 0xbeef) {
			LOG_ERR("%s: block %u of %s has wrong magic number 0%x",
			    __func__, blkno - 1, path, GET_U16(db.db_magic));
			err = ENXIO;
			done = 1;
			break;
		}
		LOG_DBG2(&fs, "%s: has %d slots\n", __func__, db.db_slots);
		while ((slotno < db.db_slots) && (done == 0)) {
			char *name;
			uint32_t ino;
			off_t new_off;

			err = efs_dir_get_dirent(&db, slotno++, &ino, &name);
			if (err != 0)
				break;
			err = efs_iget(inode->i_fs, ino, &item_inode);
			new_off = blkno * EFS_DIR_ENTRY_MOD + slotno;
			done = filler(buf, name, &item_inode->i_stat, new_off);

			LOG_DBG2(&fs, "%s: slot %u, ino %u: '%s', "
			    "new_ofs: %lu, returned %d\n", __func__, slotno,
			    ino, name, new_off, done);
			if (done)
				break;
		}
		blkno++;
	}

	LOG_DBG2(&fs, "%s: dir '%s', done - blkno=%u\n", __func__, path,
	    blkno - 1);

	return (err);
}

static int
efs_getattr(const char *path, struct stat *stbuf)
{
	efs_inode_t *inode;
	int err;

	LOG_DBG2(&fs, "%s: path='%s'\n", __func__, path);

	memset(stbuf, 0, sizeof (struct stat));

	if ((err = efs_dir_namei(&fs, path, &inode)) != 0) {
		LOG_ERR("%s: failed for '%s', error: %d\n", __func__,
		    path, err);
	    return (-err);
	}

	if (EFS_BAD_FILE(inode) != 0) {
		LOG_ERR("%s: bad file '%s'.\n", __func__, path);
		return (-EIO);
	}

	memcpy(stbuf, &inode->i_stat, sizeof (*stbuf));

	return (0);
}

static int
efs_open(const char *path, struct fuse_file_info *fi)
{
	efs_inode_t *inode;
	int err;

	err = efs_dir_namei(&fs, path, &inode);
	if (err == 0 && EFS_BAD_FILE(inode))
		err = EIO;

	LOG_DBG2(&fs, "%s: path='%s', err=%d\n", __func__, path, err);

	if (err != 0)
		LOG_ERR("cannot open file '%s', error: %n", path, err);

	return (-err);
}

static int
efs_read(const char *path, char *buf, size_t size, off_t offset,
    struct fuse_file_info *fi)
{
	efs_inode_t *inode;
	uint32_t blkno = offset / BBS;
	uint32_t nblks = size / BBS;
	int err;

	LOG_DBG2(&fs, "%s: path='%s', size=%ld, offset=%ld\n",
	    __func__, path, size, offset);

	if ((err = efs_dir_namei(&fs, path, &inode)) != 0) {
		LOG_ERR("find file '%s', error: %d\n", path, err);
		return (-err);
	}

	nblks = MIN(nblks, inode->i_nblks - blkno);

	LOG_DBG3(&fs, "%s: fixed nblks=%d\n", __func__, nblks);

	if ((err = efs_iread(inode, blkno, nblks, buf)) != 0) {
		LOG_ERR("cannot read file '%s' at offset %lu, %lu bytes\n",
		    path, offset, size);
		return (-err);
	}

	return (nblks * BBS);
}

static void
efs_destroy(void *data)
{
	ncache_destroy();
	icache_destroy();
}

struct fuse_operations efs_oper = {
	.statfs = efs_statfs,
	.readdir = efs_readdir,
	.getattr = efs_getattr,
	.open = efs_open,
	.read = efs_read,
	.destroy = efs_destroy
};

static void
usage(const char *prog_name)
{
	fprintf(stderr, "Usage: %s [options] <mountpoint>\n", prog_name);
	fprintf(stderr, "File system specific options\n");
	fprintf(stderr, "\t--partition=<N>\tNumber of partition to mount\n");
	fprintf(stderr, "\t--debug=<N>\tDebug message verbosity level (0-3)\n");
	fprintf(stderr, "\t--fs=<path>\tPath to file system image\n");
	fprintf(stderr, "\t--help | -h\tThis message\n");
}

int
main(int argc, char **argv)
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	int rc = EXIT_SUCCESS;

	/* Process options and report eventual errors. */
	options.part = -1;
	if (fuse_opt_parse(&args, &options, efs_opts, NULL) == -1)
		return (EXIT_FAILURE);

	if (options.part != -1 &&
	    (options.part < 0 || options.part > VH_PART_NUM)) {
		LOG_ERR("part_no must be 0-%d.\n", VH_PART_NUM);
		rc = EXIT_FAILURE;
	}
	if (options.log_lvl < 0 || options.log_lvl > 3) {
		LOG_ERR("debug must be between 0 and 3.\n");
		rc = EXIT_FAILURE;
	}
	if (options.fs_image == NULL) {
		LOG_ERR("file system image is not specified.\n");
		rc = EXIT_FAILURE;
	}
	if (args.argc < 2) {
		LOG_ERR("mountpoint is not specified.\n");
		rc = EXIT_FAILURE;
	}
	if (args.argc > 2) {
		printf("too many arguments.\n");
		rc = EXIT_FAILURE;
	}

	if (rc != 0 || options.show_help) {
		usage(argv[0]);
		return (rc == EXIT_SUCCESS ? 2 : rc);
	}

	fs.log_lvl = options.log_lvl;

	/*
	 * Open the file system image. We assume a EFS volume here, not sure if
	 * we can encounter a simple EFS file system too.
	 */
	if (efs_vol_open(&fs, options.fs_image, options.part) != 0) {
		rc = EXIT_FAILURE;
		goto out;
	}

	if (efs_mount(&fs) != 0) {
		efs_vol_close(&fs);
		rc = EXIT_FAILURE;
		goto out;
	}

	LOG_DBG1(&fs, "entering fuse with argc %d.\n", args.argc);

	if (fuse_main(args.argc, args.argv, &efs_oper, NULL) == -1) {
		perror("fuse");
		rc = EXIT_FAILURE;
	}

	LOG_DBG1(&fs, "fuse_main ended\n");
out:
	fuse_opt_free_args(&args);

	(void) close(fs.fd);

	return (rc);
}
