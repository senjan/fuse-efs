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

#ifndef EFS_FS_H
#define	EFS_FS_H

#include <sys/types.h>

/*
 * On-disk file system data structures.
 * Based on efs(4) shiped with Irix 6.5.30.
 * https://help.graphica.com.au/irix-6.5.30/man/4/efs
 */
#define	EFS_MAGIC	0x072959
#define	EFS_NEWMAGIC	0x07295A
#define	IS_EFS_MAGIC(x)	((x == EFS_MAGIC) || (x == EFS_NEWMAGIC))

#define	EFS_NAME	"EFS"
#define	BBS		512	/* Basic Block Size */
#define	INO_SIZE	128	/* On-disk inode size */
#define	INOS_PER_BB	(BBS / INO_SIZE)
#define	FIRST_INO	2

#define	IS_BIG_ENDIAN (!*(unsigned char *)&(uint16_t) {1})

typedef struct efs_sb {
	int32_t	s_size;		/* fs size in BBs */
	int32_t	s_first_cg;	/* start of CG in BBs */
	int32_t	s_cg_size;	/* CG size in BBs */
	int16_t	s_cg_ino_bbs;	/* num of BBs with inodes per CG */
	int16_t	s_sectors;	/* sectors per track (not used) */
	int16_t	s_heads;	/* heads per cyl (not used) */
	int16_t	s_ncg;		/* number of CGs */
	int16_t	s_dirty;	/* fsck needed (not used) */
	int16_t s_pad1;
	int32_t	s_time;		/* last sb update (not used) */
	int32_t	s_magic;	/* EFS_MAGIC or EFS_NEWMAGIC */
	char	s_fname[6];	/* file system name */
	char	s_fpack[6];	/* file system pack name */
	int32_t	s_bmsize;	/* size of bitmaps in bytes (not used) */
	int32_t	s_blk_free;	/* number of free blocks */
	int32_t	s_ino_free;	/* number of free inodes */
	int32_t	s_bmblock;	/* bitmap location (not used) */
	int32_t	s_replsb;	/* location of sb copy (not used) */
	char	s_spare[24];
	int32_t	s_checksum;	/* checksum of volume */
} efs_sb_t;

/*
 * In-core data structure.
 */
typedef struct efs_fs {
	int fd;			/* file system image file descriptor (RO) */
	off_t start;		/* in bytes */
	int log_lvl;		/* debugging log verbosity */
	efs_sb_t sb;		/* super block */
} efs_fs_t;

int efs_mount(efs_fs_t *fs);
void inode2loc(efs_fs_t *fs, uint32_t ino, uint32_t *blk, off_t *ofs);

#endif /* EFS_FS_H */
