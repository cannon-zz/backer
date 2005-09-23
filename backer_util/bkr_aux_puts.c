/*
 * int aux_puts(const char *s, char *aux, struct bkrformat *format)
 *
 * Prints the null-terminated string s in human-readable text form in the
 * auxiliary buffer pointed to by aux.  The format structure must be
 * initialized to the tape format being used.  If s is too long to fit in
 * the aux buffer then as many characters as will fit are taken from the
 * start of the string.  Returns the number of characters written.
 *
 * Extra Info: The font is a fixed-space 5x14 font.  In low density mode
 * there are 6 characters across the screen while in high density mode
 * there are 16 characters.
 *
 * Due to the characteristics of the Backer device, it is not possible to
 * leave consecutive lines blank in the video signal.  The card may choose
 * to skip over them during playback thus ruining the read-out of the video
 * field.  To prevent this from occuring, this routine will fill unused
 * lines with filler leaving a gap of one line between the filler and the
 * text.
 *
 * Copyright (C) 2000  Kipp C. Cannon
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
#include "backer.h"
#include "bkrfont.xpm"

#define  FWIDTH  5	/* font width  */
#define  FHEIGHT 14	/* font height */

int bkr_aux_puts(const char *s, char *aux, struct bkrformat *format)
{
	int  i;
	int  line, byte, bit;
	int  last_line, last_bit;
	char  c;

	memset(aux, 0, format->aux_length);

	/*
	 * Copy the font glyphs into the buffer.
	 */

	last_line = format->aux_length / format->bytes_per_line;
	if(last_line > FHEIGHT)
		last_line = FHEIGHT;
	last_bit = strlen(s) * FWIDTH;
	if(last_bit > format->bytes_per_line * 8)
		{
		last_bit = format->bytes_per_line * 8;
		last_bit -= last_bit % FWIDTH;
		}

	for(bit = 0; bit < last_bit; bit++)
		{
		c = s[bit/FWIDTH];
		if((c < ' ') || (c > '~'))
			continue;
		for(line = 0; line < last_line; line++)
			if(bkrfont_xpm[line+4][(c - ' ')*FWIDTH + bit%FWIDTH] == '+')
				aux[line*format->bytes_per_line + bit/8] |= 0x80 >> bit%8;
		}

	/*
	 * Elliminate consecutive 0's.
	 */

	for(i = 1, line = 0; line < last_line; i += 2, line++)
		for(byte = 1; byte < format->bytes_per_line - 1; i++, byte++)
			if((aux[i-1] == 0) && (aux[i] == 0) && (aux[i+1] == 0))
				aux[i] = BKR_FILLER;

	/*
	 * Fill any extra lines
	 */

	for(i = last_line*format->bytes_per_line; i < format->aux_length; aux[i++] = BKR_FILLER);

	return(bit/5);
}
