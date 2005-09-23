/*
 * Copyright (C) 2000,2001,2002  Kipp C. Cannon
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <string.h>
#include <backer.h>
#include <bkr_puts.h>
#include <bkr_font.xpm>


/*
 * Return the minimum of two values.
 */

#ifndef min
#define min(x,y) ({ \
	const typeof(x) _x = (x); \
	const typeof(y) _y = (y); \
	(void) (&_x == &_y); \
	_x < _y ? _x : _y; })
#endif


/*
 * ascii_to_glyph()
 *
 * Convert an ASCII code to a glyph number (non-printable characters are
 * mapped to ' ').
 */

static int ascii_to_glyph(char code)
{
	if((code < ' ') || (code > '~'))
		code = ' ';
	return(code - ' ');
}


/*
 * is_key_byte()
 *
 * Return true iff offset is in a key byte.
 */

static int is_key_byte(int offset, bkr_format_info_t *format)
{
	return(offset % format->key_interval == 0);
}


/*
 * screen_to_sector()
 *
 * Convert a zero-origin byte offset on the screen to a zero-origin byte
 * offset within a sector.  Returns < 0 if the specified screen location
 * does not contain a data byte (i.e. it's occupied by part of the tape
 * format itself --- "meta-data"?).
 */

static int screen_to_sector(int offset, bkr_format_info_t *format)
{
	offset -= format->leader;

	if((offset < 0) ||
	   (offset >= format->data_size))
		return(-1);

	if(format->key_length) {	/* need to correct for key bytes? */
		if(is_key_byte(offset, format))
			return(-1);
		offset -= offset/format->key_interval + 1;
	}

	return(offset);
}


/*
 * int bkr_puts()
 *
 * This function modifies the contents of sector so that when written to
 * tape, the null-terminated string s will be visible as human-readable
 * graphic text in the video signal.  The top-left corner of the text will
 * be located at the given line and bit position in the video signal.
 * Lines are counted from 0 at the top of the video field, bits from 0 at
 * the left of the video field.
 *
 * The string, s, will be truncated if needed against the last character
 * boundary prior to the right-hand side of the video image.  The actual
 * number of characters placed in the video image is returned or < 0 on
 * error.  Possible errors:  the sector format does not support bit-by-bit
 * data manipulation (it is a GCR-modulated mode).
 *
 * Extra Info: The font is a fixed-space 5x14 font.  In low density modes
 * there is a maximum of 6 characters across the screen while in high
 * density modes 16 characters will fit.  The font is stored in
 * bkr_font.xpm and can be edited at will.
 *
 * The Backer device cannot reliably retrieve data from a video signal
 * unless state transitions occur at a modest rate.  Presumably this is
 * required in order to keep a PLL properly sychronized.  Once
 * synchronization is lost, the hardware will (mostly) stop decoding data
 * altogether until the start of the next video field.  Unfortunately, the
 * graphical text data produced by this code does not, in general, satisfy
 * the minimum transition requirements of the Backer hardware.  This text,
 * therefore, must not be placed in any video field that also contains data
 * that one wishes to eventually retrieve unless the text is placed AFTER
 * that data in the sector (thus lower on the screen).
 *
 * Before actually being written to tape, the data in the sector will pass
 * through the tape data formater (eg. the device driver).  Among other
 * things, the formater randomizes the data in the sector to address the
 * issue raised above of poor PLL stability when decoding a low entropy bit
 * stream.  This means that your nice text message will be scrambled unless
 * you "pre-unrandomize" it.  This can be accomplished by using the
 * randomizer found in the device driver source tree and an example of this
 * process can be found in bkrcheck.c
 *
 * Due to the complex mapping between sector offsets and screen locations
 * it is, effectively, impossible for the calling routine to determine
 * ahead of time which specific bytes in the sector buffer will be
 * modified.  The easiest and most reliable way to determine this
 * information is to generate a "test" sector:  fill a sector buffer with
 * 0xff, with bkr_puts() print as many ' 's at the desired screen location
 * as there are characters in the string to eventually be written then
 * check to see which bytes in the sector buffer no longer contain 0xff.
 */

int bkr_puts(const char *s, unsigned char *sector, int line, int bit, bkr_format_info_t *format)
{
	int  screen_offset;     /* bit position from start of video field */
	int  row, column;       /* .xpm row and column counters */
	int  length;            /* length of string in bits */
	int  screen_mask;       /* bit mask for current screen byte */
	int  sector_offset;     /* current byte in sector */
	int  bits_per_line;     /* number of bits across one video line */

	/*
	 * Check for a valid format.
	 */

	if(format->modulation_pad)
		return(-1);

	/*
	 * Determine how many bits will be drawn in each line.
	 */

	bits_per_line = format->bytes_per_line * 8;
	screen_offset = line * bits_per_line + bit;

	length = min((int) strlen(s), (bits_per_line - screen_offset % bits_per_line) / BKR_FONT_WIDTH) * BKR_FONT_WIDTH;

	if(length <= 0)
		return(0);

	/*
	 * Draw the text.  Within this loop, bit is the bit offset from the
	 * start of the text.
	 */

	for(row = 4; row < 4+BKR_FONT_HEIGHT; screen_offset += bits_per_line, row++) {
		sector_offset = screen_to_sector(screen_offset/8, format);
		screen_mask = 0x80 >> (screen_offset & 0x7);
		column = ascii_to_glyph(s[0]) * BKR_FONT_WIDTH + 1;
		for(bit = 0; 1; bit++, column++, screen_mask >>= 1) {
			if(screen_mask == 0) {	/* move to next screen byte? */
				sector_offset = screen_to_sector((screen_offset + bit)/8, format);
				screen_mask = 0x80;
			}
			if(bit % BKR_FONT_WIDTH == 0) {	/* move to next character? */
				if(bit >= length)
					break;
				column = ascii_to_glyph(s[bit/BKR_FONT_WIDTH]) * BKR_FONT_WIDTH;
			}
			if(sector_offset >= 0) {
				if(bkr_font_xpm[row][column] == '+')
					sector[sector_offset] |= screen_mask;
				else
					sector[sector_offset] &= ~screen_mask;
			}
		}
	}

	return(length/BKR_FONT_WIDTH);
}
