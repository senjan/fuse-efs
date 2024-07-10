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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <string.h>

#include "utils.h"

#include "efs_vol.h"

static int
efs_get_vol_hdr(int fd, efs_vol_hdr_t *hdr)
{
	if (pread(fd, hdr, sizeof (*hdr), 0) != sizeof (*hdr)) {
		LOG_ERR("Cannot read volume header: %s\n", strerror(errno));
		return (EIO);
	}

	if (GET_U32(hdr->h_magic) != BOOT_BLOCK_MAGIC) {
		LOG_ERR("Wrong magic number 0x%x\n", GET_U32(hdr->h_magic));
		return (EINVAL);
	}

	return (0);
}

static int
efs_bread_common(efs_fs_t *fs, off_t offset, void *buffer, size_t bytes)
{
	/* XXX add check for reads after the end of the image */

	offset += fs->start;

	LOG_DBG2(fs, "%s: seek to 0x%lx, %ld\n", __func__, offset, offset);

	do {
		size_t got = pread(fs->fd, buffer, bytes, offset);
		if (got == -1) {
			/* syscall interrupted, retry */
			assert(errno == EINTR);
			continue;
		}
		if (got == 0)
			return (errno);
		bytes -= got;
		buffer += got;
	} while (bytes > 0);

	return (0);
}

int
efs_bread_bbs(efs_fs_t *fs, uint32_t bbs, void *buffer, uint32_t nblks)
{
	off_t offset = bbs * BBS;
	size_t bytes = nblks * BBS;

	return (efs_bread_common(fs, offset, buffer, bytes));
}

int
efs_bread(efs_fs_t *fs, uint32_t bbs, off_t ofs, void *buffer, size_t bytes)
{
	off_t offset = bbs * BBS + ofs;

	return (efs_bread_common(fs, offset, buffer, bytes));
}

int
efs_vol_open(efs_fs_t *fs, const char *fs_image, int part_no)
{
	efs_vol_hdr_t hdr;
	efs_vh_part_t *part;
	int32_t first;
	int err;

	if ((fs->fd = open(fs_image, O_RDONLY)) < 0) {
		perror("cannot open file system image");
		return (errno);
	}

	err = efs_get_vol_hdr(fs->fd, &hdr);
	if (err != 0) {
		LOG_ERR("%s: cannot read volume header\n", fs_image);
		(void) close(fs->fd);
		return (err);
	}

	LOG_DBG1(fs, "%s: volume header detected.\n", fs_image);
	if (part_no == -1) {
		/*
		 * Caller did not specify the partition to mount, choose one.
		 */
		LOG_DBG1(fs, "p#\t   start -      end\ttype\n");
		LOG_DBG1(fs, "====================================\n");
		for (int i = 0; i < VH_PART_NUM; i++) {
			part = &hdr.h_pt[i];
			first = GET_I32(part->p_first);
			int32_t cnt = GET_I32(part->p_blocks);
			int32_t type = GET_I32(part->p_type);

			if (cnt == 0)
				continue;
			if (type == PART_EFS)
				part_no = i;
			LOG_DBG1(fs, "%2i\t%8d - %8d\t%4d\n", i, first,
			    first + cnt, type);
		}
		if (part_no == -1) {
			LOG_DBG1(fs, "No suitable partition found\n");
			(void) close(fs->fd);
			return (ENXIO);
		}
		LOG_DBG1(fs, "Partition %d select. Use --partition to choose "
		    "partition manualy.\n", part_no);
	}

	part = &hdr.h_pt[part_no];
	if (GET_I32(part->p_blocks) < EFS_MIN_SIZE) {
		LOG_ERR("Partition %d is too small, it has only %d blocks.\n",
		    part_no, GET_I32(part->p_blocks));
		(void) close(fs->fd);
		return (EINVAL);
	}
	if (GET_I32(part->p_type) != PART_EFS) {
		LOG_WARN(fs, "Unexpected type of partition %d: 0x%x.\n",
		    part_no, GET_I32(part->p_type));
	}

	first = GET_I32(part->p_first);
	fs->start = first * BBS;
	LOG_DBG1(fs, "Partition %d starts at block %d, type %d.\n", part_no,
	    first, GET_I32(part->p_type));

	return (0);
}

void
efs_vol_close(efs_fs_t *fs)
{
	if (close(fs->fd) == -1) {
		LOG_ERR("Close failed: %s\n", strerror(errno));
	}
}
