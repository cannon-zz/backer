/*
 * int aux_puts(const char *s, char *aux, struct bkrformat *format)
 *
 * Prints the null-terminated string s in human-readable text form in the
 * auxiliary buffer pointed to by aux.  The format structure must be
 * initialized to the tape format being used.  If s is too long to fit in
 * the aux buffer then as many characters as will fit are taken from the
 * start of the string.  The characters are drawn taking the line of video
 * in which the buffer starts as the top row of each character;  this may
 * result in some characters losing their top row of pixels if the buffer
 * does not start at the left-most edge of the screen.  If the buffer is
 * less than 14 lines high then the bottoms of the characters will be
 * chopped off.  The number of characters written is returned.
 *
 * Extra Info:
 * The font is a fixed-space 5x14 font.  In low density mode there are 6
 * characters across the screen while in high density mode there are 16
 * characters.
 *
 * Due to the characteristics of the Backer device, it is not possible to
 * leave consecutive lines blank in the video signal.  The card may choose
 * to skip over them during playback thus ruining the read-out of the video
 * field.  To prevent this from occuring, this routine will fill unused lines
 * with filler leaving a gap of one line between the filler and the text.
 *
 * Copyright (C) 2000  Kipp C. Cannon
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

#include <string.h>
#include "backer.h"
#include "bkrfont.xpm"

#define  FWIDTH  5	/* font width  */
#define  FHEIGHT 14	/* font height */

int bkr_aux_puts(const char *s, char *aux, struct bkrformat *format)
{
	int  skipped, total_lines;
	int  i, bit, line;
	char  used[FHEIGHT];

	memset(aux, 0, format->aux_length);

	/*
	 * Determine the number of bytes in the first line that are not part of
	 * the auxiliary buffer and set the total number of lines that are to
	 * be filled.
	 */

	skipped = format->aux_offset % format->bytes_per_line;
	total_lines = (format->aux_length + skipped) / format->bytes_per_line;
	if(total_lines > FHEIGHT)
		total_lines = FHEIGHT;

	/*
	 * Shift the starting point in the text to match the location of the
	 * start of the auxiliary buffer.  If this is past the end of the
	 * string, then skip the first video line altogether.
	 */

	line = 0;
	bit = skipped * 8;
	if(bit > strlen(s) * FWIDTH)
		{
		bit = 0;
		line = 1;
		}

	/*
	 * Copy the font glyphs into the buffer bit-by-bit.
	 */

	for(; line < total_lines; line++, bit = 0)
		for(; (bit/FWIDTH < format->bytes_per_line*8/FWIDTH) &&
		      (s[bit/FWIDTH] != '\0'); bit++)
			{
			if((s[bit/FWIDTH] < ' ') || (s[bit/FWIDTH] > '~'))
				continue;
			if(font_xpm[line+4][(s[bit/FWIDTH] - ' ')*FWIDTH + bit%FWIDTH] == '+')
				aux[line*format->bytes_per_line + bit/8 - skipped] |= 0x80 >> bit%8;
			}

	/*
	 * Elliminate consecutive blank lines.
	 */

	for(line = total_lines; line; used[--line] = 0);
	for(line = skipped != 0; line < total_lines; line++)
		{
		for(i = 0; (i < format->bytes_per_line) &&
		    (aux[line*format->bytes_per_line - skipped + i] == 0); i++);
		if(i != format->bytes_per_line)
			{
			if(line > 0)
				used[line-1] = 1;
			if(line < total_lines-1)
				used[line+1] = 1;
			used[line] = 1;
			}
		}
	for(line = skipped != 0; line < total_lines; line++)
		if(!used[line])
			for(i = 0; i < format->bytes_per_line;
			    aux[line*format->bytes_per_line - skipped + i++] = BKR_FILLER);

	/*
	 * Fill any extra lines
	 */

	for(i = total_lines*format->bytes_per_line - skipped; i < format->aux_length; aux[i++] = BKR_FILLER);

	return(bit/5);
}
