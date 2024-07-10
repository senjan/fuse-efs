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

#ifndef EFS_DIR_H
#define	EFS_DIR_H

#include <sys/types.h>
#include <dirent.h>

#include "efs_file.h"
#include "efs_fs.h"

#define	EFS_DIRBLK_MAGIC	0xbeef
#define	EFS_DIRBLK_HDR_SIZE	4
#define	EFS_DIRBLK_SPACE_SIZE	(BBS - EFS_DIRBLK_HDR_SIZE)
#define	EFS_DIRBLK_SLOTS_MAX	(EFS_DIRBLK_SPACE_SIZE / 7)

#define	EFS_DIR_ENTRY_MOD	(EFS_DIRBLK_SLOTS_MAX + 1)

typedef struct efs_dirblk {
	uint16_t db_magic;
	uint8_t db_first;
	uint8_t db_slots;
	uint8_t db_space[EFS_DIRBLK_SPACE_SIZE];
} efs_dirblk_t;

/* The size of each entry is at least 6 bytes, maximal size is 260 bytes. */
typedef struct efs_dirent {
	uint32_t de_ino;
	uint8_t de_namelen;
	char de_name[1];
} efs_dirent_t;

typedef struct dir_lookup_arg {
	char *dl_name;
	uint32_t dl_ino;
	int dl_error;
} dir_lookup_arg_t;

typedef int (*dir_walker_t)(efs_dirblk_t *db, uint32_t blkno, void *arg);

int efs_dir_get_dirent(efs_dirblk_t *db, int n, uint32_t *ino, char **name);
int efs_dir_namei(efs_fs_t *fs, const char *nm, efs_inode_t **ino);
int efs_dir_walker(efs_inode_t *inode, dir_walker_t w, void *arg);

int efs_dir_lookup(efs_inode_t *inode, char *nm, uint32_t *ino);

void ncache_destroy(void);


#endif /* EFS_DIR_H */
