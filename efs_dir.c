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
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <pthread.h>

#include "utils.h"
#include "efs_vol.h"

#include "efs_dir.h"

typedef struct name_cache_item {
	const char *path;
	efs_inode_t *ino;
	struct name_cache_item *next;
} name_cache_item_t;

name_cache_item_t *ncache = NULL;
static pthread_mutex_t ncache_mtx = PTHREAD_MUTEX_INITIALIZER;

#ifdef EFS_DEBUG
static void
efs_print_dir(char *buf)
{
	efs_dirblk_t *db = (efs_dirblk_t *)buf;
	uint16_t magic = GET_U16(db->db_magic);
	int i;

	if (magic != EFS_DIRBLK_MAGIC) {
		LOG_ERR("%s: wrong dirblk magic 0x%x\n", __func__,
		    GET_U16(db->db_magic));
		return;
	}
	printf("%s: first=%d, slots=%d\n", __func__, db->db_first,
	    db->db_slots);

	for (i = 0; i < db->db_slots; i++) {
		int ofs = (db->db_space[i]) << 1;
		efs_dirent_t *de;
		int j;

		printf("%i: %d (%d)\n", i, db->db_space[i], ofs);
		if (ofs == 0)
			continue;
		de = (efs_dirent_t *)(buf + ofs);
		printf("ino=%d, nlen=%d: ", GET_U32(de->de_ino),
		    de->de_namelen);
		for (j = 0; j < de->de_namelen; j++)
			putchar(de->de_name[j]);
		putchar('\n');
	}
}
#endif

static int
efs_db_lookup(efs_dirblk_t *db, char *name, uint32_t *ino)
{
	uint16_t magic = GET_U16(db->db_magic);
	size_t name_len = strlen(name);
	int i;

	if (magic != EFS_DIRBLK_MAGIC) {
		LOG_ERR("%s: wrong dirblk magic 0x%x\n", __func__,
		    GET_U16(db->db_magic));
		return (ENXIO);
	}

	for (i = 0; i < db->db_slots; i++) {
		int ofs = (db->db_space[i]) << 1;
		efs_dirent_t *de;

		if (ofs == 0)
			continue;
		de = (efs_dirent_t *)((char *)db + ofs);
		if (name_len != de->de_namelen)
			continue;
		if (strncmp(name, (const char *)&de->de_name,
		    de->de_namelen) == 0) {
			*ino = GET_U32(de->de_ino);
			return (0);
		}
	}
	return (ENOENT);
}

int
efs_dir_get_dirent(efs_dirblk_t *db, int n, uint32_t *ino, char **name)
{
	efs_dirent_t *de;
	char *nm;
	int offset;

	if (GET_U16(db->db_magic) != EFS_DIRBLK_MAGIC) {
		LOG_ERR("%s: wrong dirblk magic 0x%x\n", __func__,
		    GET_U16(db->db_magic));
		return (ENXIO);
	}
	if (n < 0 || n >= db->db_slots)
		return (ENOENT);

	offset = db->db_space[n] << 1;
	de = (efs_dirent_t *)((char *)db + offset);
	*ino = GET_U32(de->de_ino);
	nm = malloc(de->de_namelen + 1);
	strncpy(nm, de->de_name, de->de_namelen);
	nm[de->de_namelen] = '\0';
	*name = nm;

	return (0);
}

static efs_inode_t *
ncache_search(const char *nm)
{
	name_cache_item_t *ci;

	for (ci = ncache; ci != NULL; ci = ci->next) {
		if (strcmp(ci->path, nm) == 0)
			return (ci->ino);
	}
	return (NULL);
}

static void
ncache_add(const char *nm, efs_inode_t *inode)
{
	name_cache_item_t *ci;

	LOG_DBG2(inode->i_fs, "%s: adding inode %d for '%s'\n",
	    __func__, inode->i_num, nm);

	if ((ci = malloc(sizeof (*ci))) == NULL)
		return;
	ci->path = strdup(nm);
	ci->ino = inode;
	ci->next = ncache;
	ncache = ci;
}

void
ncache_destroy(void)
{
	name_cache_item_t *ci = ncache;

	while (ci != NULL) {
		name_cache_item_t *next = ci->next;
		free(ci);
		ci = next;
	}
}

int
efs_dir_namei(efs_fs_t *fs, const char *nm, efs_inode_t **ino)
{
	efs_inode_t *inode = NULL;
	uint32_t cur_ino = FIRST_INO;
	char *path;
	char *cur;
	int err = 0;

	/* Try the cache first */
	pthread_mutex_lock(&ncache_mtx);
	if ((inode = ncache_search(nm)) != NULL) {
		LOG_DBG2(fs, "%s: found cached inode %d for '%s'\n", __func__,
		    inode, nm);
		*ino = inode;
		goto out;
	}

	if ((path = strdup(nm)) == NULL) {
		err = ENOMEM;
		goto out;
	}

	assert(path[0] == '/');	/* Must be an absolute path */

	cur = &path[1];	/* skip / */

	while (1) {
		char *next;

		if ((err = efs_iget(fs, cur_ino, &inode)) != 0)
			break;

		if (cur == NULL || *cur == '\0') {
			break;	/* done */
		}

		/* get path component to search in this directory */
		next = strchr(cur, '/');
		if (next != NULL)
			*next = '\0';

		if ((err = efs_dir_lookup(inode, cur, &cur_ino)) != 0)
			break;

		if (next != NULL) {
			/* move after /, to next component (if any) */
			*next = '/';
			cur = next + 1;
		} else {
			/*
			 * We reached the end of the path. We need to call
			 * efs_iget() to get the inode and finish.
			 */
			cur = NULL;
		}
	}

	free(path);

	if (err == 0) {
		ncache_add(nm, inode);
		*ino = inode;
		LOG_DBG2(fs, "found inode %d for '%s'\n", inode->i_num, nm);
	}
out:
	pthread_mutex_unlock(&ncache_mtx);
	if (err != 0)
		LOG_DBG2(fs, "%s: failed for '%s' with %d\n",
		    __func__, nm, err);
	return (err);
}

callback_state_t
dir_lookup_cb(efs_inode_t *inode, uint32_t blkno, uint32_t offset,
    void *arg)
{
	dir_lookup_arg_t *dl = (dir_lookup_arg_t *)arg;
	efs_dirblk_t *db;
	int ret;

	if ((db = malloc(BBS)) == NULL)
		return (ENOMEM);

	if ((ret = efs_bread_bbs(inode->i_fs, blkno, db, 1)) != 0)
		return (ERROR);

	ret = efs_db_lookup(db, dl->dl_name, &dl->dl_ino);
	free(db);
	LOG_DBG2(inode->i_fs, "%s: inode %d, blkno %d, offset %d, name '%s'."
	    "Got %d\n", __func__, inode->i_num, blkno, offset, dl->dl_name,
	    ret);

	if (ret == ENOENT)
		return (CONTINUE);
	if (ret != 0) {
		dl->dl_error = ret;
		return (ERROR);
	}

	/* name found */
	assert(dl->dl_ino > 0);
	return (STOP);
}

int
efs_dir_lookup(efs_inode_t *inode, char *nm, uint32_t *ino)
{
	dir_lookup_arg_t arg = { 0 };

	arg.dl_name = nm;
	arg.dl_ino = 0;

	assert(inode != NULL);
	LOG_DBG1(inode->i_fs, "%s: searcing '%s' in dir inode %d\n", __func__,
	    nm, inode->i_num);

	if (!IS_DIR(inode))
		return (ENOTDIR);

	(void) efs_walk(inode, 0, 0, dir_lookup_cb, &arg);

	if (arg.dl_ino != 0) {
		assert(arg.dl_error == 0);
		*ino = arg.dl_ino;
		LOG_DBG1(inode->i_fs, "%s: found inode %d\n", __func__,
		    arg.dl_ino);
		return (0);
	}

	return (ENOENT);
}
