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

#ifndef UTILS_H
#define	UTILS_H

#include <sys/types.h>

#if !defined(__BYTE_ORDER__)
#error __BYTE_ORDER__ must be defined!
#endif

/* EFS is always big-endian */

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define	GET_U16(x)	swap_uint16(x)
#define	GET_I16(x)	swap_int16(x)
#define	GET_U32(x)	swap_uint32(x)
#define	GET_I32(x)	swap_int32(x)
#else
#define	GET_U16(x)	(x)
#define	GET_I16(x)	(x)
#define	GET_U32(x)	(x)
#define	GET_I32(x)	(x)
#endif

#define	MIN(x, y) ((x) > (y) ? (y) : (x))
#define	MAX(x, y) ((x) < (y) ? (y) : (x))

void logger(int level, int msg_level, char *msg, ...);

#define	LOG_DBG3(fs, msg...) logger((fs)->log_lvl, 4, msg)
#define	LOG_DBG2(fs, msg...) logger((fs)->log_lvl, 3, msg)
#define	LOG_DBG1(fs, msg...) logger((fs)->log_lvl, 2, msg)
#define	LOG_WARN(fs, msg...) logger((fs)->log_lvl, 1, msg)
#define	LOG_ERR(msg...) logger(0, 0, msg)

uint16_t swap_uint16(uint16_t val);
int16_t swap_int16(int16_t val);
uint32_t swap_uint32(uint32_t val);
int32_t swap_int32(int32_t val);

#endif /* UTILS_H */
