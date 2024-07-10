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

#ifndef EFS_VOL_H
#define	EFS_VOL_H

#include <sys/types.h>

#include "efs_fs.h"

/*
 * On-disk volume data structure.
 */

#define	BOOT_BLOCK_MAGIC 0xbe5a941

#define	VD_NAME_LEN	8
#define	VH_BFILE_LEN	16
#define	VH_VOLDIR_NUM	15
#define	VH_PART_NUM	16

#define	PART_EFS	5
#define	PART_WD		6	/* probably the whole disk */

#define	EFS_MIN_SIZE	10	/* minimal EFS size (an arbitrary value) */

typedef struct efs_vh_dir {
	char	v_name[VD_NAME_LEN];
	int32_t	v_lbn;
	int32_t	v_nbytes;
} efs_vh_dir_t;

typedef struct efs_vh_part {
	int32_t	p_blocks;
	int32_t	p_first;
	int32_t	p_type;
} efs_vh_part_t;

typedef struct efs_vol_hdr {
	uint32_t h_magic;
	int16_t	h_root;
	int16_t	h_swap;
	char	h_bfile[VH_BFILE_LEN];
	char	h_pad[48];
	efs_vh_dir_t	h_vd[VH_VOLDIR_NUM];
	efs_vh_part_t	h_pt[VH_PART_NUM];
	int32_t	h_cksum;
	int32_t h_pad2;
} efs_vol_hdr_t;


int efs_bread_bbs(efs_fs_t *fs, uint32_t bbs, void *buffer, uint32_t nblks);
int efs_bread(efs_fs_t *fs, uint32_t bbs, off_t ofs, void *buffer,
    size_t bytes);
int efs_vol_open(efs_fs_t *fs, const char *fs_image, int part_no);
void efs_vol_close(efs_fs_t *fs);

#endif /* EFS_VOL_H */
