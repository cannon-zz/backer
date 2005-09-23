/*
 * backer.h
 * 
 * Header file for Backer 16/32 device driver.
 *
 * Copyright (C) 2000,2001  Kipp C. Cannon
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

#ifndef  _BACKER_H
#define  _BACKER_H

/*
 * Hardware stuff
 */

#define  BKR_BIT_HIGH_DENSITY   0x01
#define  BKR_BIT_DMA_REQUEST    0x02
#define  BKR_BIT_DATA           0x04
#define  BKR_BIT_SYNC           0x08
#define  BKR_BIT_FRAME_BUSY     0x10
#define  BKR_BIT_TRANSMIT       0x20
#define  BKR_BIT_RECEIVE        0x40
#define  BKR_BIT_NTSC_VIDEO     0x80

#define  BKR_LINE_PERIOD        64      /* microseconds (NTSC and PAL) */
#define  BKR_FIRST_LINE_NTSC    10      /* first line of data in video field */
#define  BKR_FIRST_LINE_PAL     6       /* ditto */


/*
 * Mode masks and flags.
 *
 * Used widely throughout the code to describe the operating mode.
 * Returned by MTIOCGET in mtget.mt_dsreg.  Also appears in the status file
 * in the /proc file system.
 */

#define  BKR_VIDEOMODE(x)      ((x) & 0x0003)
#define  BKR_DENSITY(x)        ((x) & 0x000c)
#define  BKR_FORMAT(x)         ((x) & 0x0030)
#define  BKR_NTSC              0x0001           /* NTSC video mode */
#define  BKR_PAL               0x0002           /* PAL video mode */
#define  BKR_LOW               0x0004           /* Low density */
#define  BKR_HIGH              0x0008           /* High density */
#define  BKR_RAW               0x0000           /* read/write raw data */
#define  BKR_SP                0x0010           /* read/write SP/LP format */
#define  BKR_EP                0x0020           /* read/write EP format */


/*
 * Format information.
 */

typedef struct
	{
	unsigned int  bytes_per_line;   /* width of one line of video */
	unsigned int  field_size;       /* bytes in an even video field */
	unsigned int  interlace;        /* difference between odd/even fields */
	unsigned int  frame_size;       /* bytes in a full video frame */
	unsigned int  leader;           /* see diagram above */
	unsigned int  trailer;          /* see diagram above */
	unsigned int  active_size;      /* see diagram above */
	unsigned int  key_interval;     /* key byte spacing */
	unsigned int  key_length;       /* number of key bytes */
	unsigned int  gcr;              /* 1 == use GCR modulation */
	unsigned int  modulation_pad;   /* extra space for modulation overhead */
	unsigned int  buffer_size;      /* see diagram above */
	unsigned int  interleave;       /* block interleave */
	unsigned int  parity_size;      /* see diagram above */
	unsigned int  data_size;        /* see diagram above */
	} bkr_format_info_t;

#define BKR_FORMAT_INFO_INITIALIZER ( (bkr_format_info_t [])                                  \
{ {  4, 1012,  4, 2028,  40, 32,  940,  42, 22, 1, 124,  816, 12,  96,  720 },      /* nle */ \
  {  4, 1012,  4, 2028,   0,  0, 1012,  -1,  0, 0,   0, 2028,  1,   0, 2028 },      /* nlr */ \
  {  4, 1012,  4, 2028,  32, 28,  952,  45, 21, 0,  21,  931,  7,  70,  861 },      /* nls */ \
  {  4, 1220,  0, 2440,  48, 36, 1136,  39, 29, 1, 152,  984, 12,  96,  888 },      /* ple */ \
  {  4, 1220,  0, 2440,   0,  0, 1220,  -1,  0, 0,   0, 2440,  1,   0, 2440 },      /* plr */ \
  {  4, 1220,  0, 2440,  40, 36, 1144,  47, 24, 0,  24, 1120,  8,  80, 1040 },      /* pls */ \
  { 10, 2530, 10, 5070, 100, 70, 2360,  81, 29, 1, 288, 2072, 28, 224, 1848 },      /* nhe */ \
  { 10, 2530, 10, 5070,   0,  0, 2530,  -1,  0, 0,   0, 5070,  1,   0, 5070 },      /* nhr */ \
  { 10, 2530, 10, 5070,  80, 70, 2380, 119, 20, 0,  20, 2360, 20, 200, 2160 },      /* nhs */ \
  { 10, 3050,  0, 6100, 120, 90, 2840,  88, 32, 1, 344, 2496, 26, 208, 2288 },      /* phe */ \
  { 10, 3050,  0, 6100,   0,  0, 3050,  -1,  0, 0,   0, 6100,  1,   0, 6100 },      /* phr */ \
  { 10, 3050,  0, 6100, 100, 90, 2860, 130, 22, 0,  22, 2838, 22, 220, 2618 } } )   /* phs */

#define BKR_NUM_FORMATS (sizeof(BKR_FORMAT_INFO_INITIALIZER)/sizeof(bkr_format_info_t))


/*
 * Convert a mode (as above) to an index into the format array.
 */

static inline int bkr_mode_to_format(int mode)
{
	int  format = 0;

	if(BKR_DENSITY(mode) == BKR_HIGH)
		format += 6;
	if(BKR_VIDEOMODE(mode) == BKR_PAL)
		format += 3;
	if(BKR_FORMAT(mode) == BKR_SP)
		format += 2;
	else if(BKR_FORMAT(mode) == BKR_RAW)
		format += 1;

	return(format);
}


/*
 * _sysctl() interface
 */

enum
	{
	DEV_BACKER=6	/* FIXME:  get a number (from LANANA?) */
	};

enum
	{
	BACKER_STATUS=1,
	BACKER_FORMAT=2
	};

#endif /* _BACKER_H */
