#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Comment this out if your hardware doesn't support this op code.
 */

#define ROL(x, n)  asm("rolw %2, %0" : "=r" (x) : "0" (x), "c" (n))

#ifndef __fswab16
static __inline__ __const__ unsigned short __fswab16(unsigned short x)
{
	__asm__("xchgb %b0,%h0" \
	        : "=q" (x) \
	        :  "" (x)); \
        return x;
}
#endif

#define  GCR_MASK  ((unsigned short) 0x01ff)
#define  BUFF_CHUNKS  1000

void encode(void);
void decode(void);

static unsigned short gcr_encode[] =
	{ 124, 125, 120, 121, 123, 113, 115, 114,
	  119, 118, 116,  97,  99,  98, 103, 102,
	  100, 110, 108, 109, 104, 105, 107,  65,
	   67,  66,  71,  70,  68,  78,  76,  77,
	   72,  73,  75,  94,  92,  93,  88,  89,
	   91,  81,  83,  82, 248, 249, 251, 241,
	  243, 242, 247, 246, 244, 225, 227, 226,
	  231, 230, 228, 238, 236, 237, 232, 233,
	  235, 193, 195, 194, 199, 198, 196, 206,
	  204, 205, 200, 201, 203, 222, 220, 221,
	  216, 217, 219, 209, 211, 210, 215, 214,
	  131, 130, 135, 134, 132, 142, 140, 141,
	  136, 137, 139, 158, 156, 157, 152, 153,
	  155, 145, 147, 146, 151, 150, 148, 190,
	  188, 189, 184, 185, 187, 177, 179, 178,
	  183, 182, 180, 161, 163, 162, 167, 166,
	  164, 497, 499, 498, 503, 502, 500, 481,
	  483, 482, 487, 486, 484, 494, 492, 493,
	  488, 489, 491, 449, 451, 450, 455, 454,
	  452, 462, 460, 461, 456, 457, 459, 478,
	  476, 477, 472, 473, 475, 465, 467, 466,
	  471, 470, 387, 386, 391, 390, 388, 398,
	  396, 397, 392, 393, 395, 414, 412, 413,
	  408, 409, 411, 401, 403, 402, 407, 406,
	  404, 446, 444, 445, 440, 441, 443, 433,
	  435, 434, 439, 438, 436, 417, 419, 418,
	  423, 422, 420, 430, 428, 429, 263, 262,
	  260, 270, 268, 269, 264, 265, 267, 286,
	  284, 285, 280, 281, 283, 273, 275, 274,
	  279, 278, 276, 318, 316, 317, 312, 313,
	  315, 305, 307, 306, 311, 310, 308, 289,
	  291, 290, 295, 294, 292, 302, 300, 301 };

static unsigned char gcr_decode[512];



int main(int argc, char *argv[])
{
	int i;

	memset(gcr_decode, 0, 512);

	for(i = 0; i < 256; i++)
		gcr_decode[gcr_encode[i]] = i;

	if(argc > 1)
		{
		if(argv[1][1] == 'u')
			decode();
		}
	else
		encode();

	exit(0);
}


void encode(void)
{
	char shift;
	unsigned short state, tmp;
	unsigned char buff[BUFF_CHUNKS * 9];
	unsigned char *in_pos, *out_pos;

	state = 0;
	while(1)
		{
		in_pos = buff + BUFF_CHUNKS;
		out_pos = buff;
		fread(in_pos, 1, BUFF_CHUNKS * 8, stdin);
		if(feof(stdin))
			break;
		tmp = 0;
		shift = 7;
		while(1)
			{
			if(state &= 1)
				state = GCR_MASK;
			state ^= gcr_encode[*(in_pos++)];
			tmp |= state << shift;
			tmp = (tmp >> 8) | (tmp << 8);
			if(--shift >= 0)
				{
				*(out_pos++) = tmp;  /* write low byte */
				tmp &= (unsigned short) 0xff00;
				}
			else
				{
				*(unsigned short *) out_pos = tmp;
				out_pos++; out_pos++;
				if(out_pos >= in_pos)
					break;
				tmp = 0;
				shift = 7;
				}
			}
		fwrite(buff, 1, BUFF_CHUNKS * 9, stdout);
		}
}


void decode(void)
{
	char shift;
	unsigned short state, tmp;
	unsigned char buff[BUFF_CHUNKS * 9];
	unsigned char *in_pos, *out_pos;

	state = 0;
	while(1)
		{
		fread(buff, 1, BUFF_CHUNKS * 9, stdin);
		if(feof(stdin))
			break;
		in_pos = out_pos = buff;
		shift = 1;
		while(1)
			{
			tmp  = *(in_pos++);
			tmp |= *in_pos << 8;
			/*
			tmp = *(unsigned short *) (in_pos++);
			*/
#ifdef ROL
			ROL(tmp, shift);
#else
			tmp = (tmp >> 8) | (tmp << 8);
			tmp >>= 8 - shift;
#endif
			if(state & 1)
				tmp = ~tmp;
			state ^= tmp;
			*(out_pos++) = gcr_decode[tmp & GCR_MASK];
			if(++shift > 8)
				{
				if(++in_pos - out_pos >= BUFF_CHUNKS)
					break;
				shift = 1;
				}
			}
		fwrite(buff, 1, BUFF_CHUNKS * 8, stdout);
		}
}
