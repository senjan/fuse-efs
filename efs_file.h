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

#ifndef EFS_FILE_H
#define	EFS_FILE_H

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "efs_fs.h"

#define	EFS_DIRECTEXTENTS	12

/*
 * On-disk data structures
 * Based on inode(4) shiped with Irix 6.5.30.
 * https://help.graphica.com.au/irix-6.5.30/man/4/inode
 */

typedef struct efs_od_extent {
	uint32_t ext1;
	uint32_t ext2;
} efs_od_extent_t;

#define	EXT_MAGIC(x) (x >> 24)
#define	EXT_BN(x) (x & 0xffffff)
#define	EXT_LEN(x) (x >> 24)
#define	EXT_OFFSET(x) (x & 0xffffff)

#define	EFS_EXTENTS_PER_BB	(BBS / sizeof (efs_od_extent_t))

typedef struct efs_od_inode {
	uint16_t	di_mode;
	int16_t		di_nlink;
	uint16_t	di_uid;
	uint16_t	di_gid;
	int32_t		di_size;	/* file size in bytes, off_t */
	uint32_t	di_atime;	/* time_t */
	uint32_t	di_mtime;	/* time_t */
	uint32_t	di_ctime;	/* time_t */
	int32_t		di_gen;	/* long */
	int16_t		di_nextents;
	uint8_t		di_version;
	uint8_t		di_unused;
	union {
		/* array of extents */
		efs_od_extent_t	di_extents[EFS_DIRECTEXTENTS];
		/* symlink target */
		char		di_symlink[sizeof (efs_od_extent_t) *
		    EFS_DIRECTEXTENTS];
		/* device */
		uint32_t	di_dev;	/* dev_t */
	} di_u;
} efs_od_inode_t;

/* In-core structures */

typedef struct efs_extent {
	uint32_t e_offset;
	uint32_t e_blk;
	uint16_t e_len;
} efs_extent_t;

typedef struct efs_inode {
	efs_od_inode_t	i_od;	/* on-disk inode */
	uint32_t	i_num;	/* inode number */
	mode_t		i_mode;	/* OS native file mode */
	efs_fs_t	*i_fs;	/* file system structure */
	struct stat	i_stat;	/* OS native file stats */
	uint16_t	i_nextents;	/* number of extents */
	efs_extent_t	*i_extents;	/* array of extents */
	uint32_t	i_nblks;	/* blocks incl holes */
	uint32_t	i_nalloc_blks;	/* allocated blocks */
	int		i_flags;
	struct efs_inode *i_next;
} efs_inode_t;

#define	IS_DIR(inode)	((inode->i_mode & S_IFMT) == S_IFDIR)

/* In-core inode flags */
#define	EFS_FLG_BAD_FILE	1

#define	EFS_BAD_FILE(i)	((i->i_flags & EFS_FLG_BAD_FILE) != 0)

typedef enum callback_state {
	CONTINUE,
	STOP,
	ERROR
} callback_state_t;

typedef callback_state_t (*file_walker_t)(efs_inode_t *inode, uint32_t blkno,
    uint32_t offset, void *arg);

/* Public inode related functions */
int efs_iget(efs_fs_t *fs, uint32_t ino, efs_inode_t **inode);
int efs_iread(efs_inode_t *inode, uint32_t blkno, uint32_t nblks, void *buf);
int efs_walk(efs_inode_t *inode, uint32_t blkno, uint32_t nblks,
    file_walker_t w, void *arg);

void icache_destroy(void);


#ifdef EFS_DEBUG
/* for debug and reverse eng. only */
void efs_print_inode(efs_od_inode_t *inode);
void efs_print_dir(char *buf);
#endif

#endif /* EFS_FILE_H */
