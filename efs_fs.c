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
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>

#include "utils.h"
#include "efs_vol.h"

#include "efs_fs.h"

int
efs_mount(efs_fs_t *fs)
{
	int32_t sb_magic;
	int err;

	if ((err = efs_bread(fs, 1, 0, &fs->sb, sizeof (fs->sb))) != 0) {
		LOG_ERR("cannot read super block: %s\n", strerror(err));
		return (err);
	}

	sb_magic = GET_I32(fs->sb.s_magic);

	LOG_DBG1(fs, "super block magic is 0x%x\n", sb_magic);
	if (!IS_EFS_MAGIC(sb_magic)) {
		LOG_ERR("invalid super block magic\n");
		return (EINVAL);
	}
	LOG_DBG1(fs, "super block: size %d (%dKB), blk/ino free: %d/%d, "
	    "CGs: %d, CG size: %d, CG ino: %d, first CG: %d\n",
	    GET_I32(fs->sb.s_size), GET_I32(fs->sb.s_size) / 2,
	    GET_I32(fs->sb.s_blk_free), GET_I32(fs->sb.s_ino_free),
	    GET_I16(fs->sb.s_ncg), GET_I32(fs->sb.s_cg_size),
	    GET_I16(fs->sb.s_cg_ino_bbs), GET_I32(fs->sb.s_first_cg));
	LOG_DBG2(fs, "super block: name='%s', pack='%s'\n", fs->sb.s_fname,
	    fs->sb.s_fpack);

	return (0);
}

void
inode2loc(efs_fs_t *fs, uint32_t ino, uint32_t *blk, off_t *ofs)
{
	int32_t cgsize = GET_I32(fs->sb.s_cg_size);
	int32_t firstcg = GET_I32(fs->sb.s_first_cg);
	int32_t ino_bbs_per_cg = GET_I16(fs->sb.s_cg_ino_bbs);
	int32_t inos_per_cg = ino_bbs_per_cg * INOS_PER_BB;

	uint32_t cg = ino / inos_per_cg;
	uint32_t cgofs = (ino % inos_per_cg) / INOS_PER_BB;
	uint32_t bbofs = firstcg + cg * cgsize + cgofs;
	uint32_t idx = ino & (INOS_PER_BB - 1);

	assert((cg * ino_bbs_per_cg * INOS_PER_BB + idx +
	    cgofs * INOS_PER_BB) == ino);

	*blk = bbofs;
	*ofs = idx * INO_SIZE;

	LOG_DBG2(fs, "%s:  ino=%d -> blk=%d, ofs=%ld\n", __func__, ino, *blk,
	    *ofs);
}
