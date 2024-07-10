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
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>

#include "utils.h"
#include "efs_vol.h"
#include "efs_dir.h"

#include "efs_file.h"

static efs_inode_t *icache = NULL;
static pthread_mutex_t icache_mtx = PTHREAD_MUTEX_INITIALIZER;

static void
efs_inode_stat(efs_inode_t *inode,  struct stat *stbuf)
{
	stbuf->st_mode = GET_U16(inode->i_od.di_mode);
	stbuf->st_ino = inode->i_num;
	stbuf->st_dev = 0;
	stbuf->st_rdev = 0;
	stbuf->st_nlink = GET_U16(inode->i_od.di_nlink);
	stbuf->st_uid = GET_U16(inode->i_od.di_uid);
	stbuf->st_gid = GET_U16(inode->i_od.di_gid);
	stbuf->st_size = GET_I32(inode->i_od.di_size);
	stbuf->st_atime = GET_U32(inode->i_od.di_atime);
	stbuf->st_mtime = GET_U32(inode->i_od.di_mtime);
	stbuf->st_ctime = GET_U32(inode->i_od.di_ctime);
	stbuf->st_blksize = BBS;
	stbuf->st_blocks = stbuf->st_size / 512 + 1;
#if defined(__SOLARIS__)
	(void) strncpy(stbuf->st_fstype, EFS_NAME, _ST_FSTYPSZ);
#endif
	LOG_DBG2(inode->i_fs, "%s: ino=%ld, nlink=%d, mode=0o%o, blks=%ld\n",
	    __func__, stbuf->st_ino, stbuf->st_nlink, stbuf->st_mode,
	    stbuf->st_blocks);
}

static int
efs_inode_load_indirect(efs_inode_t *inode, uint32_t indblkno,
    efs_extent_t *ext, int *extn)
{
	efs_od_extent_t indext[BBS / sizeof (efs_od_extent_t)];
	int ret;

	LOG_DBG2(inode->i_fs, "%s: indblkno=%d, extn=%d\n", __func__,
	    indblkno, *extn);

	if ((ret = efs_bread_bbs(inode->i_fs, indblkno, indext, 1)) != 0)
		return (ret);

	for (int i = 0; i < EFS_EXTENTS_PER_BB; i++) {
		uint32_t ext1 = GET_U32(indext[i].ext1);
		uint32_t ext2 = GET_U32(indext[i].ext2);
		if (EXT_MAGIC(ext1) != 0) {
			LOG_ERR("inode %d, extent %d has wrong magic 0x%x\n",
			    __func__, inode->i_num, i, EXT_MAGIC(ext1));
			break;
		}
		ext[i].e_blk = EXT_BN(ext1);
		ext[i].e_len = EXT_LEN(ext2);
		ext[i].e_offset = EXT_OFFSET(ext2);
		(*extn)++;

		LOG_DBG2(inode->i_fs, "%02d: %d -> %d - %d\n", i,
		    ext[i].e_offset, ext[i].e_blk,
		    ext[i].e_blk + ext[i].e_len - 1);
	}

	return (0);
}

static int
efs_inode_verify_extents(efs_inode_t *inode)
{
	efs_extent_t *ext = inode->i_extents;
	uint32_t blocks = 0;
	uint32_t allocated_blocks = 0;
	int i;

	LOG_DBG1(inode->i_fs, "%s: inode %d has %d extents, flags=%x\n",
	    __func__, inode->i_num, inode->i_nextents, inode->i_flags);

	for (i = 0; i < inode->i_nextents; i++) {
		blocks = ext[i].e_offset + ext[i].e_len;
		allocated_blocks += ext[i].e_len;
	}

	inode->i_nblks = blocks;
	inode->i_nalloc_blks = allocated_blocks;
	LOG_DBG1(inode->i_fs, "%s: inode %d has %d blocks, %d allocated\n",
	    __func__, inode->i_num, blocks, allocated_blocks);
	assert(inode->i_nalloc_blks <= inode->i_nblks);

	return (0);
}

static int
efs_inode_load_extents(efs_inode_t *inode)
{
	uint16_t n = GET_I16(inode->i_od.di_nextents);
	boolean_t direct = (n <= EFS_DIRECTEXTENTS);
	efs_extent_t *ext;
	int max_extn;	/* max number of extents (for indirect blocks ) */
	int extn;	/* actual number of extents */
	int err;

	if (direct) {
		/* Inode has direct blocks */
		if ((ext = calloc(n, sizeof (efs_extent_t))) == NULL)
			return (ENOMEM);

		LOG_DBG2(inode->i_fs, "%s: inode %d has %n direct extents",
		    __func__, inode->i_num, n);

		for (int i = 0; i < n; i++) {
			uint32_t ext1 =
			    GET_U32(inode->i_od.di_u.di_extents[i].ext1);
			uint32_t ext2 =
			    GET_U32(inode->i_od.di_u.di_extents[i].ext2);
			if (EXT_MAGIC(ext1) != 0) {
				LOG_ERR("%s: inode %d extent %d has wrong "
				    "magic 0x%x\n", __func__, inode->i_num, i,
				    EXT_MAGIC(ext1));
				free(ext);
				return (EINVAL);
			}
			ext[i].e_blk = EXT_BN(ext1);
			ext[i].e_len = EXT_LEN(ext2);
			ext[i].e_offset = EXT_OFFSET(ext2);
			LOG_DBG2(inode->i_fs, "%02d: %d -> %d - %d\n", i,
			    ext[i].e_offset, ext[i].e_blk,
			    ext[i].e_blk + ext[i].e_len - 1);
		}

		inode->i_nextents = n;
		inode->i_extents = ext;
		return (0);
	}

	n = EXT_OFFSET(GET_U32(inode->i_od.di_u.di_extents[0].ext2));
	extn = 0;
	max_extn = n * EFS_EXTENTS_PER_BB;
	if ((ext = calloc(max_extn, sizeof (efs_extent_t))) == NULL)
		return (ENOMEM);

	LOG_DBG2(inode->i_fs, "%s: indirect n=%d, max_extn=%d\n", __func__, n,
	    max_extn);

	err = EINVAL;		/* No extent found */
	for (int i = 0; i < n; i++) {
		uint32_t ext1 = GET_U32(inode->i_od.di_u.di_extents[i].ext1);

		if (EXT_MAGIC(ext1) != 0) {
			LOG_ERR("%s: inode %d extent %d has wrong magic 0x%x\n",
			    __func__, inode->i_num, i, EXT_MAGIC(ext1));
			return (EINVAL);
		}
		err = efs_inode_load_indirect(inode, EXT_BN(ext1),
		    &ext[i * EFS_EXTENTS_PER_BB], &extn);
		if (err != 0)
			break;
	}

	if (err == 0) {
		inode->i_nextents = extn;
		inode->i_extents = ext;
	} else {
		free(ext);
	}

	return (err);
}

int
efs_iget(efs_fs_t *fs, uint32_t ino, efs_inode_t **inode)
{
	efs_inode_t *i = icache;
	uint32_t blkno;
	off_t ofs;
	int err = 0;

	assert(inode != NULL);

	LOG_DBG2(fs, "iget inode %d\n", ino);

	pthread_mutex_lock(&icache_mtx);
	while (i != NULL) {
		if (i->i_num == ino) {
			assert(i != NULL);
			*inode = i;
			LOG_DBG2(fs, "iget: inode %d found in icache\n", ino);
			pthread_mutex_unlock(&icache_mtx);
			return (0);
		}
		i = i->i_next;
	}

	/* requested inode is not in icache - load it from the disk */
	if ((i = malloc(sizeof (efs_inode_t))) == NULL) {
		err = ENOMEM;
		goto out;
	}
	memset(inode, 0, sizeof (*inode));
	inode2loc(fs, ino, &blkno, &ofs);

	err = efs_bread(fs, blkno, ofs, &i->i_od, sizeof (efs_od_inode_t));
	if (err != 0)
		goto out;

	/* add inode to the cache */
	i->i_next = icache;
	icache = i;

	/* fill cached items */
	i->i_num = ino;
	i->i_fs = fs;
	efs_inode_stat(i, &i->i_stat);
	i->i_mode = i->i_stat.st_mode;
	i->i_flags = 0;
	if ((err = efs_inode_load_extents(i)) != 0)
		i->i_flags |= EFS_FLG_BAD_FILE;
	(void) efs_inode_verify_extents(i);
	*inode = i;
out:
	pthread_mutex_unlock(&icache_mtx);
	return (err);
}

int
efs_iread(efs_inode_t *inode, uint32_t blkno, uint32_t nblks, void *buf)
{
	uint32_t blkend = blkno + nblks - 1;
	int err = 0;

	LOG_DBG2(inode->i_fs, "%s: inode %d, blkno %d, nblks %d\n", __func__,
	    inode->i_num, blkno, nblks);

	memset(buf, 0, nblks * BBS);

	if (nblks == 0)
		return (0);

	if (blkno >= inode->i_nblks)
		return (ENXIO);
	nblks = MIN(nblks, inode->i_nblks - blkno);
	for (int i = 0; i < inode->i_nextents; i++) {
		efs_extent_t *e = &inode->i_extents[i];
		uint32_t e_last = e->e_blk + e->e_len;
		uint32_t start;
		uint32_t to_read;
		uint32_t buf_off;
		void *cur_buf;

		LOG_DBG3(inode->i_fs, "%d: b=%d, l=%d, o=%d\n", i, e->e_blk,
		    e->e_len, e->e_offset);
		if (e->e_offset + e->e_len < blkno)
			continue;
		if (e->e_offset > blkend)
			break;
		start = blkno - e->e_offset;
		buf_off = start - blkno;
		to_read = MIN(nblks - buf_off, e_last - start);

		LOG_DBG3(inode->i_fs, "start=%d, to_read=%d, buf_off=%d\n",
		    start, to_read, buf_off);
		if (to_read == 0)
			break;
		cur_buf = buf + buf_off * BBS;

		err = efs_bread_bbs(inode->i_fs, start + e->e_blk, cur_buf,
		    to_read);
		if (err != 0) {
			LOG_ERR("%s: cannot read inode %d, offset %d, err=%d\n",
			    __func__, inode->i_num, buf_off, err);
			break;
		}
	}

	return (err);
}

int
efs_walk(efs_inode_t *inode, uint32_t blkno, uint32_t nblks, file_walker_t w,
    void *arg)
{
	uint32_t cur_blkno = 0;
	int ret = ENOENT;

	LOG_DBG2(inode->i_fs, "%s: inode=%d, blkno=%d, nblks=%d\n", __func__,
	    inode->i_num, blkno, nblks);

	for (int e = 0; e < inode->i_nextents; e++) {
		efs_extent_t *cext = &inode->i_extents[e];
		uint32_t ext_ofs;

		cur_blkno = cext->e_offset;
		if ((cur_blkno + cext->e_len) < blkno) /* fast track */
			continue;
		ext_ofs = cur_blkno - blkno;
		for (; ext_ofs < cext->e_len; ext_ofs++) {
			callback_state_t st;
			if (nblks != 0 && cur_blkno > blkno + nblks)
				break;
			st = w(inode, cext->e_blk + ext_ofs, cur_blkno, arg);
			if (st == ERROR)
				return (1);	/* XXX set error in arg */
			if (st != CONTINUE)
				break;
			cur_blkno++;
		}
	}

	return (ret);
}

void
icache_destroy(void)
{
	efs_inode_t *ino;
	unsigned long cnt = 0;

	pthread_mutex_lock(&icache_mtx);

	ino = icache;
	while (ino != NULL) {
		efs_inode_t *next = ino->i_next;
		free(ino);
		cnt++;
		ino = next;
	}
	pthread_mutex_unlock(&icache_mtx);
}

#ifdef EFS_DEBUG
void
efs_print_inode(efs_od_inode_t *inode)
{
	int16_t nextents = GET_I16(inode->di_nextents);
	int i;

	(void) printf("mode: 0x%x, nlink: %d, owner: %d/%d, size: %dB\n",
	    GET_U16(inode->di_mode), GET_I16(inode->di_nlink),
	    GET_I16(inode->di_uid), GET_I16(inode->di_gid),
	    GET_I32(inode->di_size));
	(void) printf("gen: %d, nextents: %d, version: %d\n",
	    GET_I32(inode->di_version), nextents, inode->di_version);

	for (i = 0; i < nextents; i++) {
		uint32_t ext1 = GET_U32(inode->di_u.di_extents[i].ext1);
		uint32_t ext2 = GET_U32(inode->di_u.di_extents[i].ext2);

		(void) printf("%2d: m: %d, bn: %d, len: %d, offs: %d\n", i,
		    EXT_MAGIC(ext1), EXT_BN(ext1), EXT_LEN(ext2),
		    EXT_OFFSET(ext2));
	}
}
#endif
