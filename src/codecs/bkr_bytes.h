/*
 * Driver for Danmere's Backer 16/32 video tape backup cards.
 *
 * Copyright (C) 2001,2002 Kipp C. Cannon
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

#ifndef _BKR_BYTES_H
#define _BKR_BYTES_H

#ifdef __KERNEL__

#include <linux/types.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>

#define bswap_16(x) __swab16(x)

#if    defined(__LITTLE_ENDIAN) && !defined(__BIG_ENDIAN)
#define __BKR_BYTE_ORDER __LITTLE_ENDIAN
#elif !defined(__LITTLE_ENDIAN) &&  defined(__BIG_ENDIAN)
#define __BKR_BYTE_ORDER __BIG_ENDIAN
#else
#error  Unknown/unsupported byte order!
#endif

#else /* __KERNEL__ */

#include <sys/types.h>
#include <endian.h>
#include <byteswap.h>

#define __BKR_BYTE_ORDER __BYTE_ORDER

/*
 * Machine-dependant byte order compensation.
 */

#if __BKR_BYTE_ORDER == __LITTLE_ENDIAN
#define __le32_to_cpu(x)  ((u_int32_t)(x))
#define __le16_to_cpu(x)  ((u_int16_t)(x))
#define __cpu_to_le32(x)  ((u_int32_t)(x))
#define __cpu_to_be16(x)  bswap_16(x)
#elif __BKR_BYTE_ORDER == __BIG_ENDIAN
#define __le32_to_cpu(x)  bswap_32(x)
#define __le16_to_cpu(x)  bswap_16(x)
#define __cpu_to_le32(x)  bswap_32(x)
#define __cpu_to_be16(x)  ((u_int16_t)(x))
#else
#error Unknown/unsupported byte order!
#endif	/* __BKR_BYTE_ORDER */


/*
 * Unaligned memory access.  If your system can handle unaligned memory
 * accesses, then use the first two macros;  otherwise use the second
 * two.  The utilities' Makefile is setup so as to cause GCC to generate an
 * error if you use the "unaligned-OK" macros on a machine that can't do
 * it.
 */

#ifndef UNALIGNED_NOT_OK

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

#endif	/* UNALIGNED_NOT_OK */

#endif	/* __KERNEL__ */

#endif	/* _BKR_BYTES_H */
