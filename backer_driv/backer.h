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

#define  BKR_BIT_HIGH_DENSITY  0x01
#define  BKR_BIT_DMA_REQUEST   0x02
#define  BKR_BIT_DATA          0x04
#define  BKR_BIT_SYNC          0x08
#define  BKR_BIT_FRAME_BUSY    0x10
#define  BKR_BIT_TRANSMIT      0x20
#define  BKR_BIT_RECEIVE       0x40
#define  BKR_BIT_NTSC_VIDEO    0x80

#define  BYTES_PER_LINE_LOW    4
#define  BYTES_PER_LINE_HIGH   10
#define  LINES_PER_FIELD_NTSC  253
#define  LINES_PER_FIELD_PAL   305


/*
 * Used widely throughout the code to describe the operating mode.
 * Returned by MTIOCGET in mtget.mt_dsreg.  Also appears in
 * /proc/driver/backer.
 */

#define  BKR_VIDEOMODE(x)      ((x) & 0x0003)
#define  BKR_DENSITY(x)        ((x) & 0x000c)
#define  BKR_FORMAT(x)         ((x) & 0x0030)
#define  BKR_NTSC              0x0001           /* NTSC video mode */
#define  BKR_PAL               0x0002           /* PAL video mode */
#define  BKR_LOW               0x0004           /* Low density */
#define  BKR_HIGH              0x0008           /* High density */
#define  BKR_RAW               0x0000           /* read/write raw data */
#define  BKR_SP                0x0010           /* VCR is in SP/LP mode */
#define  BKR_EP                0x0020           /* VCR is in EP mode */


#endif /* _BACKER_H */
