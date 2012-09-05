#include <stdio.h>
#include <stdlib.h>
#include "backer.h"

#define  KEY_LENGTH  32

#if 0
#define  get_random  (fgetc(randomdev) >> 5)
#else
#define  get_random  ((random() >> 18) & 7)
#endif

int  count_bits(unsigned int);
void print_table(char *, unsigned int *, int, int);
int  is_good_rll(int, int);
int  nrzi_to_nrz(int, int);
void print_bits(int, int);

int main(void)
{
	int  i, j, count;
	unsigned int  table[256];
#if 0
	int  weights[] = { 255, 247, 219, 163, 93, 37, 9, 1, 0 };
#endif
	FILE  *randomdev;

	randomdev = fopen("/dev/random", "r");

	for(i = 0; i < KEY_LENGTH; i++)
		for(j = -1, count = table[i] = 0; (count_bits(table[i]) < 4) || (j < i) || !is_good_rll(table[i], 8);)
			{
			table[i] ^= 1 << get_random;
			for(j = 0; (j < i) && (table[j] != table[i]); j++);
			}
	print_table("static unsigned char  key", table, 8, KEY_LENGTH);


#if 0
	for(i = 0; i < 256; i++)
		table[i] = weights[count_bits(i)];
	print_table("static unsigned char  weight", table, 8, 256);
#endif


	table[0] = -1;
	for(i = 0; 1; i++)
		{
		while(!is_good_rll(++table[i], 9));
		if(i >= 255)
			break;
		table[i+1] = table[i];
		}
	print_table("static u_int16_t  gcr_encode", table, 9, 256);
	printf("(Max rll code: %d)\n\n", table[255]);

	printf("  NRZ       RLL       NRZ       RLL       NRZ       RLL       NRZ       RLL   \n");
	for(i = 0; i < 64; i++)
		{
		print_bits(i, 8);
		putchar(' ');
		print_bits(table[i], 9);
		putchar(' ');
		putchar(' ');
		print_bits(i+64, 8);
		putchar(' ');
		print_bits(table[i+64], 9);
		putchar(' ');
		putchar(' ');
		print_bits(i+128, 8);
		putchar(' ');
		print_bits(table[i+128], 9);
		putchar(' ');
		putchar(' ');
		print_bits(i+192, 8);
		putchar(' ');
		print_bits(table[i+192], 9);
		printf("\n");
		}


	exit(0);
}

int  count_bits(unsigned int n)
{
	unsigned int  count;

	for(count = 0; n; n >>= 1)
		count += n & 1;

	return(count);
}

void print_table(char *name, unsigned int *buff, int bits, int n)
{
	int  i = 0;

	printf("%s[] =\n\t{ ", name);
	while(1)
		{
		if(buff[i] == 0)
			printf("0x%#0*x", (bits+3)/4, 0);
		else
			printf("%#0*x", 2 + (bits+3)/4, buff[i]);
		if(++i == n)
			break;
		if((i & 7) == 0)
			printf(",\n\t  ");
		else
			printf(", ");
		}
	printf(" };\n\n");
}


int is_good_rll(int rll, int bits)
{
	int i;
	int zeros = 2;
	int ones = 2;

	for(i = 1 << (bits-1); i > 0; i >>= 1)
		{
		if(rll & i)
			{
			/*
			if(++ones > 6)
				return(0);
			*/
			zeros = 0;
			}
		else
			{
			/*
			if(rll < 194)
				{
				if(++zeros > 3)
					return(0);
				}
			else
				{
				if(++zeros > 4)
					return(0);
				}
			*/
			if(rll < 180)
				{
				if(++zeros > 2)
					return(0);
				}
			else
				{
				if(++zeros > 3)
					return(0);
				}
			ones = 0;
			}
		}
	if(zeros > 2)
		return(0);
	if(ones > 2)
		return(0);
	return(1);
}

int nrzi_to_nrz(int nrzi, int bits)
{
	int i, result = 0;

	for(i = 1 << (bits-1); i > 0; i >>= 1)
		if(nrzi & i)
			result ^= (i << 1) - 1;
	return(result);
}

void print_bits(int value, int n)
{
	for(n = 1 << (n - 1); n > 0; n >>= 1)
		putchar((value & n) ? '1' : '0');
}
