/*
 * linux_compat.h
 *
 * Replacement for Linux kernel headers if they aren't available.
 *
 * Copyright (C) 2001 Kipp C. Cannon
 * 
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _LINUX_COMPAT_H_
#define _LINUX_COMPAT_H_

/*
 * Byte swaping for big endian <---> little endian conversion.
 */

/*
 * Map BSD-like constants to the ones we want.
 */

#if !defined __BYTE_ORDER
#if defined BYTE_ORDER
#define __BYTE_ORDER BYTE_ORDER
#else
#error Can't figure out byte order.
#endif
#if defined LITTLE_ENDIAN && !defined __LITTLE_ENDIAN
#define __LITTLE_ENDIAN LITTLE_ENDIAN
#endif
#if defined BIG_ENDIAN && !defined __BIG_ENDIAN
#define __BIG_ENDIAN BIG_ENDIAN
#endif


static inline u_int16_t __swab16(u_int16_t x)
{
	return(((x & (u_int16_t) 0xff00) >> 8) |
	       ((x & (u_int16_t) 0x00ff) << 8));
}

static inline u_int32_t __swab32(u_int32_t x)
{
	return(((x & (u_int32_t) 0xff000000) >> 24) |
	       ((x & (u_int32_t) 0x00ff0000) >>  8) |
	       ((x & (u_int32_t) 0x0000ff00) <<  8) |
	       ((x & (u_int32_t) 0x000000ff) << 24));
}


#if __BYTE_ORDER == __LITTLE_ENDIAN
#define __le32_to_cpu(x)  (x)
#define __cpu_to_le32(x)  (x)
#define __cpu_to_be16(x)  __swab16(x)
#else
#define __le32_to_cpu(x)  __swab32(x)
#define __cpu_to_le32(x)  __swab32(x)
#define __cpu_to_be16(x)  (x)
#endif


/*
 * Unaligned memory access.  If your system can handle unaligned memory
 * accesses, then use the first two macros;  otherwise use the second
 * two.  If unsure, use the second two.
 */

#if 0

#define get_unaligned(ptr) (*(ptr))
#define put_unaligned(val, ptr) ((void)( *(ptr) = (val) ))

#else

#include <string.h>
#define get_unaligned(ptr) \
  ({ __typeof__(*(ptr)) __tmp; memcpy(&__tmp, (ptr), sizeof(*(ptr))); __tmp; })
#define put_unaligned(val, ptr)               \
  ({ __typeof__(*(ptr)) __tmp = (val);        \
     memcpy((ptr), &__tmp, sizeof(*(ptr)));   \
     (void)0; })

#endif


#endif /* _LINUX_COMPAT_H_ */
