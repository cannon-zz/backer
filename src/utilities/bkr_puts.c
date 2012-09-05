/*
 * Copyright (C) 2000,2001,2002,2008  Kipp C. Cannon
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
 * ============================================================================
 *
 *                             Format Information
 *
 * ============================================================================
 */


struct bkr_puts_format bkr_puts_get_format(enum bkr_videomode v, enum bkr_bitdensity d, enum bkr_sectorformat f)
{
	switch(d) {
	case BKR_LOW:
		switch(v) {
		case BKR_NTSC:
			switch(f) {
			case BKR_EP:
				return (struct bkr_puts_format) { 40,  720,  44, 22,  4};
			case BKR_SP:
				return (struct bkr_puts_format) { 32,  830,  45, 22,  4};
			/* FIXME:  what to do about this? */
			/*case BKR_RAW:
				return (struct bkr_puts_format) {  0, 1012,   0,  0,  4};*/
			}
		case BKR_PAL:
			switch(f) {
			case BKR_EP:
				return (struct bkr_puts_format) { 48,  888,  40, 29,  4};
			case BKR_SP:
				return (struct bkr_puts_format) { 40,  980,  49, 24,  4};
			/* FIXME:  what to do about this? */
			/*case BKR_RAW:
				return (struct bkr_puts_format) {  0, 1220,   0,  0,  4};*/
			}
		}
	case BKR_HIGH:
		switch(v) {
		case BKR_NTSC:
			switch(f) {
			case BKR_EP:
				return (struct bkr_puts_format) {100, 1848,  84, 29, 10};
			case BKR_SP:
				return (struct bkr_puts_format) { 80, 2160, 125, 20, 10};
			/* FIXME:  what to do about this? */
			/*case BKR_RAW:
				return (struct bkr_puts_format) {  0, 2530,   0,  0, 10};*/
			}
		case BKR_PAL:
			switch(f) {
			case BKR_EP:
				return (struct bkr_puts_format) {120, 2288,  91, 32, 10};
			case BKR_SP:
				return (struct bkr_puts_format) {100, 2618, 136, 22, 10};
			/* FIXME:  what to do about this? */
			/*case BKR_RAW:
				return (struct bkr_puts_format) {  0, 3050,   0,  0, 10};*/
			}
		}
	}

	return (struct bkr_puts_format) {0,};
}


/*
 * ============================================================================
 *
 *                                Helper Code
 *
 * ============================================================================
 */


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
 * Return true iff offset is in a key byte.
 */


static int is_key_byte(int offset, const struct bkr_puts_format *format)
{
	return(offset % format->key_interval == 0);
}


/*
 * Convert a zero-origin byte offset on the screen to a zero-origin byte
 * offset within a sector.  Returns < 0 if the specified screen location
 * does not contain a data byte (i.e. it's occupied by part of the tape
 * format itself --- "meta-data"?).
 */


static int screen_to_sector(int offset, const struct bkr_puts_format *format)
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
 * ============================================================================
 *
 *                                 bkr_puts()
 *
 * ============================================================================
 */


/*
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
 * data manipulation (it is an RLL-modulated mode).
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
 * that data in the sector (lower on the screen).
 *
 * Before being written to tape, the data in the sector will pass through a
 * variety of formating and conditiong stages.  Among other things, this
 * involves randomizing the data to help ensure bit state transitions occur
 * frequently enough.  This means that your nice text message will be
 * scrambled.  The randomizer function is its own inverse, however, so if
 * you apply the randomizer to your rendered text frames before sending
 * them through the data formatter, the formatter will in effect
 * "unrandomize" the text and it will be displayed properly.  This can be
 * accomplished by using the randomizer found in the codec source and an
 * example of this process can be found in bkrcheck.c.
 *
 * Due to the complex mapping between sector offsets and screen locations
 * it is, effectively, impossible for the calling routine to determine
 * ahead of time which specific bytes in the sector buffer will be
 * modified.  The easiest and most reliable way to determine this
 * information is to generate a "test" sector:  fill a sector buffer with
 * 0xff, then with bkr_puts() print as many ' 's at the desired screen
 * location as there are characters in the string to eventually be written,
 * then check to see which bytes in the sector buffer no longer contain
 * 0xff.
 */


int bkr_puts(const char *s, unsigned char *sector, int line, int bit, const struct bkr_puts_format *format)
{
	int  screen_offset;     /* bit position from start of video field */
	int  row, column;       /* .xpm row and column counters */
	int  length;            /* length of string in bits */
	int  screen_mask;       /* bit mask for current screen byte */
	int  sector_offset;     /* current byte in sector */
	int  bits_per_line;     /* number of bits across one video line */

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
		sector_offset = screen_to_sector(screen_offset / 8, format);
		screen_mask = 0x80 >> (screen_offset & 0x7);
		column = ascii_to_glyph(s[0]) * BKR_FONT_WIDTH + 1;
		for(bit = 0; 1; bit++, column++, screen_mask >>= 1) {
			if(screen_mask == 0) {	/* move to next screen byte? */
				sector_offset = screen_to_sector((screen_offset + bit) / 8, format);
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

	return length / BKR_FONT_WIDTH;
}
