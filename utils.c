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
#include <stdarg.h>

#include "utils.h"

uint16_t
swap_uint16(uint16_t val)
{
	return (val << 8) | (val >> 8);
}

int16_t
swap_int16(int16_t val)
{
	return (val << 8) | ((val >> 8) & 0xFF);
}

uint32_t
swap_uint32(uint32_t val)
{
	val = ((val << 8) & 0xFF00FF00) | ((val >> 8) & 0xFF00FF);
	return (val << 16) | (val >> 16);
}

int32_t
swap_int32(int32_t val)
{
	val = ((val << 8) & 0xFF00FF00) | ((val >> 8) & 0xFF00FF);
	return (val << 16) | ((val >> 16) & 0xFFFF);
}

void
logger(int level, int msg_level, char *msg, ...)
{
	va_list argp;

	if (msg_level > level)
		return;

	if (msg_level == 0)
		fprintf(stderr, "Error: ");

	va_start(argp, msg);

	vfprintf(msg_level == 0 ? stderr : stdout, msg, argp);

	va_end(argp);
}
