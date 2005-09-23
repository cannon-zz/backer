/*
 * Reed-Solomon coding and decoding
 *
 * This code is an overhaul of a Reed-Solomon encoder/decoder implementation
 * written by Phil Karn which was distributed under the name rs-2.0.
 *
 * Copyright (C) 2000,2001 Kipp C. Cannon
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
 *
 * For more information on error control coding, the following is a list of
 * references I have found helpful.
 *
 * Blahut, Richard E., Theory and Practice of Error Control Codes, 1983,
 *	Addison-Wesley Inc.
 *
 * Lin, Shu, and Daniel Costello Jr., Error Control Coding: Fundamentals
 * 	and Applications, 1983, Prentice-Hall Inc.
 *
 * Consultative Committe for Space Data Systems, Telemetry Channel Coding
 *	(CCSDS 101.0-B-4), May 1999.
 */

/*
 * To use this in the Linux kernel different header choices need to be
 * made.
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif /* __KERNEL__ */

#include "rs.h"


/*
 * Compute x % NN, where NN is 2^MM-1, without a divide.
 */

static inline gf modNN(int x)
{
	while(x >= NN)
		{
		x -= NN;
		x = (x >> MM) + (x & NN);
		}
	return(x);
}

static inline int min(int a, int b)
{
	return(a <= b ? a : b);
}


/*
 * Decrement for location variable in Chien search
 */

#if   (LOG_BETA==1)              /* conventional Reed-Solomon */
#define Ldec  1
#elif (LOG_BETA==11) && (MM==8)  /* CCSDS Reed-Solomon */
#define Ldec  116
#else                            /* anything else */
static int Ldec;
static void gen_ldec(void)
{
	for(Ldec = 1; (Ldec % LOG_BETA) != 0; Ldec += NN);
	Ldec /= LOG_BETA;
}
#endif /* LOG_BETA */


/*
 * generate_GF()
 *
 * Constructs look-up tables for performing arithmetic over the Galois
 * field GF(2^MM) from the irreducible polynomial p(x) stored in p.
 *
 * This function constructs the look-up tables called Log_alpha[] and
 * Alpha_exp[] from p.  Since 0 cannot be represented as a power of alpha,
 * special elements are added to the look-up tables to represent this case
 * and all multiplications must first check for this.  We call this special
 * element INFINITY since it is the result of taking the log (base alpha)
 * of zero.
 *
 * The bits of the parameter p specify the generator polynomial with the
 * LSB being the co-efficient of x^0 and the MSB being the coefficient of
 * x^MM.  For example, if
 *
 *          p = 013
 *
 * (i.e. 13 octal) then
 *
 *          p(x) = x^3 + x + 1
 *
 * Note that the coefficient of x^MM must = 1.
 *
 * For field generator polynomials, see Lin & Costello, Appendix A, and Lee
 * and Messerschmitt, pg. 453.  Field generator polynomials for symbol
 * sizes from 2 to 16 bits can be found in reed_solomon_init() below.
 */

static gf  Alpha_exp[2*NN];     /* exponent->polynomial conversion table */
static gf  Log_alpha[NN + 1];   /* polynomial->exponent conversion table */
#define    INFINITY  (NN)       /* representation of 0 in exponent form */

static void generate_GF(int p)
{
	int i;

	for(i = 0; i < MM; i++)
		Alpha_exp[i] = 1 << i;

	Alpha_exp[MM] = p & NN;

	for(i++; i < NN; i++)
		{
		Alpha_exp[i] = Alpha_exp[i-1] << 1;
		if(Alpha_exp[i-1] & (1 << (MM-1)))
			Alpha_exp[i] = (Alpha_exp[i] & NN) ^ Alpha_exp[MM];
		}

	for(i = 0; i < NN; i++)
		Log_alpha[Alpha_exp[i]] = i;
	Log_alpha[0] = INFINITY;

	/*
	 * Duplicate Alpha_exp[] to reduce the use of modNN()
	 */

	memcpy(&Alpha_exp[NN], Alpha_exp, NN * sizeof(gf));
}


/*
 * generate_poly()
 *
 * Constructs the generator polynomial for the Reed-Solomon code of length
 * NN (=2^MM-1) with the number of parity symbols being given by
 * rs_format->parity.
 *
 * The co-efficients of the generator polynomial are left in exponent form
 * for better encoder performance but note that it is still faster to
 * compute them in polynomial form and convert them afterwards.
 */

static void generate_poly(struct rs_format_t *rs_format)
{
	int  i, j;

	rs_format->g[0] = 1;

	for(i = 0; i < rs_format->parity; i++)
		{
		rs_format->g[i+1] = 1;
		for(j = i; j > 0; j--)
			{
			if(rs_format->g[j] != 0)
				rs_format->g[j] = rs_format->g[j-1] ^ Alpha_exp[modNN(Log_alpha[rs_format->g[j]] + LOG_BETA*(J0 + i))];
			else
				rs_format->g[j] = rs_format->g[j-1];
			}
		rs_format->g[0] = Alpha_exp[modNN(Log_alpha[rs_format->g[0]] + LOG_BETA*(J0 + i))];
		}

	for(i = 0; i <= rs_format->parity; i++)
		rs_format->g[i] = Log_alpha[rs_format->g[i]];
}


/*
 * reed_solomon_encode()
 *
 * The code symbols that are returned from this encoder are given with the
 * lowest order term at offset 0 in the array so we have
 *
 *    c(x) = c_NN * x^NN + c_(NN-1) * x^(NN-1) + ... + c_0 * x^0
 *
 * and
 *
 *             block[] = { c_0, ..., c_NN }
 *
 * and the parity symbols appear at the bottom of the array.  One must
 * leave enough room for them by having the data start at offset (n-k) in
 * the array.  A side benefit of this is that it reminds the programmer
 * that the array must be allocated large enough to hold not only their
 * data but also the parity symbols.
 *
 * The encoding algorithm is the long division algorithm but where we only
 * care about the remainder.  The remainder is computed in place and the
 * algorithm is as follows (recall that all operations are performed over
 * GF(2^MM) so addition is the same as subtraction):
 *	1.  Initialise the remainder to 0.
 *	2.  Starting from the highest order data symbol...
 *	3.  Add the highest element of the remainder to the current data
 *	    symbol to compute a feed-back multiplier.
 *	4.  Shift the remainder one to the left so 0 -> b0, b0 -> b1, etc.
 *	5.  Multiply each co-efficient of the generator polynomial by the
 *	    feedback and add the resulting polynomial to the remainder.
 *	6.  Get the next data symbol and goto step 3 until all data symbols
 *	    have been processed.
 *	7.  The remainder forms the parity symbols.
 */

void reed_solomon_encode(data_t *block, struct rs_format_t *rs_format)
{
	int  i;                         /* current data symbol index */
	data_t  *b;                     /* current remainder symbol */
	gf  *g;                         /* current gen. poly. symbol */
	gf   feedback;                  /* feed-back multiplier */

	if(rs_format->parity == 0)
		return;

	memset(block, 0, rs_format->parity * sizeof(data_t));

	/*
	 * At the start of each iteration, b points to the most significant
	 * symbol in the remainder for that iteration.
	 */

	b = &block[rs_format->remainder_start];

	for(i = rs_format->n - 1; i >= rs_format->parity; i--)
		{
		feedback = Log_alpha[block[i] ^ *b];
		if(feedback != INFINITY)
			{
			b--;
			for(g = &rs_format->g[rs_format->parity - 1]; b >= block; g--, b--)
				if(*g != INFINITY)
					*b ^= Alpha_exp[feedback + *g];
			for(b = &block[rs_format->parity - 1]; g > rs_format->g; b--, g--)
				if(*g != INFINITY)
					*b ^= Alpha_exp[feedback + *g];
			*b = Alpha_exp[feedback + *g];
			}
		else
			*b = 0;
		if(--b < block)
			b = &block[rs_format->parity - 1];
		}
}


/*
 * reed_solomon_decode()
 *
 * The bulk of this code has been taken from Phil Karn's decoder
 * implementation but with many minor modifications to things like the
 * directions of for loops, their bounds, etc. all with an eye to
 * performance.  The fundamental algorithm itself, however, has not been
 * changed.  The algorithms used here are the Berlekamp-Massey algorithm
 * for finding the error locations and the Forney algorithm for finding the
 * error magnitudes.
 */

int reed_solomon_decode(data_t *block, gf *erasure, int no_eras, struct rs_format_t *rs_format)
{
	int  i, j, k;                   /* general purpose loop indecies */
	gf  *x, *y;                     /* general purpose loop pointers */
	int  deg_lambda, deg_omega;     /* degrees of lambda(x) and omega(x) */
	gf  tmp;                        /* temporary storage */
	gf  discr;                      /* discrepancy in Berlekamp-Massey algo. */
	gf  num, den;                   /* numerator & denominator for Forney alg. */
	static gf temp[MAX_PARITY+1];   /* temporary storage polynomial */
	static gf s[MAX_PARITY];        /* syndrome polynomial */
	static gf lambda[MAX_PARITY+1]; /* error & erasure locator polynomial */
	static gf b[MAX_PARITY+1];      /* shift register storage for B-M alg. */
	static gf omega[MAX_PARITY];    /* error & erasure evaluator polynomial */
	static gf root[MAX_PARITY];     /* roots of lambda */
	static gf loc[MAX_PARITY];      /* error locations (reciprocals of root[]) */
	int  count = 0;                 /* number of roots of lambda */

	if(rs_format->parity == 0)
		return(0);

	/*
	 * Compute the syndromes by evaluating block(x) at the roots of
	 * g(x), namely beta^(J0+i), i = 0, ... ,(n-k-1).  When finished,
	 * convert them to alpha-rep and test for all zero.  If the
	 * syndromes are all zero, block[] is a codeword and there are no
	 * errors to correct.
	 */

	for(x = &s[rs_format->parity]; x > s; *(--x) = block[0]);

	for(j = 1; j < rs_format->n; j++)
		if(block[j] != 0)
			{
			tmp = modNN(Log_alpha[block[j]] + LOG_BETA*J0*j);
			for(x = s; x < &s[rs_format->parity]; x++)
				{
				*x ^= Alpha_exp[tmp];
				#if (LOG_BETA==1)
				if((tmp += j) >= NN)
					tmp -= NN;
				#else
				tmp = modNN(tmp + LOG_BETA*j);
				#endif /* LOG_BETA */
				}
			}
	for(x = &s[rs_format->parity-1]; *x == 0; x--)
		{
		if(x == s)
			return(0);
		*x = INFINITY;
		}
	for(; x >= s; x--)
		*x = Log_alpha[*x];

	/*
	 * Init lambda to be the erasure locator polynomial
	 */

	lambda[0] = 1;
	memset(&lambda[1], 0, rs_format->parity*sizeof(gf));

	if(no_eras > 0)
		{
		lambda[1] = Alpha_exp[modNN(LOG_BETA*erasure[0])];
		for(i = 1; i < no_eras; i++)
			{
			tmp = modNN(LOG_BETA*erasure[i]);
			for(y = &lambda[i+1]; y > lambda; y--)
				if(*(y-1) != 0)
					*y ^= Alpha_exp[tmp + Log_alpha[*(y-1)]];
			}
		}
	deg_lambda = no_eras;

	/*
	 * Berlekamp-Massey algorithm to determine error+erasure locator
	 * polynomial.  See Blahut, R., Theory and Practice of Error
	 * Control Codes, section 7.5.
	 */

	for(i = rs_format->parity; i > deg_lambda; i--)
		b[i] = INFINITY;
	for(; i >= 0; i--)
		b[i] = Log_alpha[lambda[i]];

	for(j = no_eras; j < rs_format->parity; j++)	/* j+1 is "r", the step number */
		{
		/* compute discrepancy */
		discr = 0;
		for(i = deg_lambda; i >= 0; i--)
			if((lambda[i] != 0) && (s[j - i] != INFINITY))
				discr ^= Alpha_exp[Log_alpha[lambda[i]] + s[j - i]];

		discr = Log_alpha[discr];

		if(discr == INFINITY)
			{
			/* b(x) <-- x*b(x) */
			memmove(&b[1], &b[0], rs_format->parity * sizeof(gf));
			b[0] = INFINITY;
			continue;
			}

		/* temp(x) <-- lambda(x) - discr*x*b(x) */
		for(i = rs_format->parity; i >= 1; i--)
			{
			if(b[i-1] != INFINITY)
				temp[i] = lambda[i] ^ Alpha_exp[discr + b[i-1]];
			else
				temp[i] = lambda[i];
			}
		temp[0] = lambda[0];

		if(2 * deg_lambda <= j + no_eras)
			{
			deg_lambda = j+1 + no_eras - deg_lambda;
			/* b(x) <-- discr^-1 * lambda(x) */
			for(i = rs_format->parity; i >= 0; i--)
				{
				if(lambda[i] != 0)
					b[i] = modNN(NN - discr + Log_alpha[lambda[i]]);
				else
					b[i] = INFINITY;
				}
			}
		else
			{
			/* b(x) <-- x*b(x) */
			memmove(&b[1], &b[0], rs_format->parity * sizeof(gf));
			b[0] = INFINITY;
			}

		/* lambda(x) <-- temp(x) */
		memcpy(lambda, temp, (rs_format->parity + 1) * sizeof(gf));
		}

	for(i = rs_format->parity; i > deg_lambda; i--)
		lambda[i] = INFINITY;
	for(; i >= 0; i--)
		lambda[i] = Log_alpha[lambda[i]];

	/*
	 * Find roots of the error & erasure locator polynomial by Chien
	 * search (i.e. trial and error).  At each iteration, i, the j-th
	 * component of temp[] contains the j-th coefficient of lambda
	 * multiplied by alpha^(i*j).  We get components of temp[] for the
	 * next iteration by multiplying each by alpha^j.  Note that the
	 * 0-th coefficient of lambda is always 1 so we can handle it
	 * explicitly.
	 */

	memcpy(&temp[1], &lambda[1], deg_lambda * sizeof(gf));

#if LOG_BETA == 1
	for(i = 1; i <= NN; i++)
#else
	for(i = 1, k = NN-Ldec; i <= NN; i++, k = modNN(NN - Ldec + k))
#endif
		{
		tmp = 1;
		for(j = deg_lambda, x = &temp[deg_lambda]; j > 0; x--, j--)
			if(*x != INFINITY)
				{
				*x = modNN(*x + j);
				tmp ^= Alpha_exp[*x];
				}

		if(tmp != 0)
			continue;

		/*
		 * Store root and error location number.  If we've found as
		 * many roots as lambda(x) has then abort to save time.
		 */

		root[count] = i;
#if LOG_BETA == 1
		loc[count] = NN - i;
#else
		loc[count] = k;
#endif
		if(loc[count] >= rs_format->n)
			return(-1);
		if(++count == deg_lambda)
			break;
		}

	/*
	 * deg(lambda) != number of roots then there are degenerate roots
	 * which means we've detected an uncorrectable error.
	 */

	if(deg_lambda != count)
		return(-1);

	/*
	 * Compute error & erasure evaluator poly omega(x) = s(x)*lambda(x)
	 * (modulo x^(n-k)) in alpha rep.  Also find deg(omega).
	 */

	memset(omega, 0, rs_format->parity * sizeof(gf));
	for(i = deg_lambda; i >= 0; i--)
		{
		if(lambda[i] == INFINITY)
			continue;
		for(k = rs_format->parity - 1, y = &s[k - i]; y >= s; k--, y--)
			if(*y != INFINITY)
				omega[k] ^= Alpha_exp[lambda[i] + *y];
		}
	for(x = &omega[rs_format->parity - 1]; (x >= omega) && (*x == 0); x--)
		*x = INFINITY;
	for(deg_omega = x - omega; x >= omega; x--)
		*x = Log_alpha[*x];

  
	/*
	 * Forney algorithm for computing error magnitudes.  If X_l^-1 is
	 * the l-th root of lambda (X_l is the l-th error location) then
	 * Y_l, the magnitude of the l-th error, is given  by
	 *
	 *                            omega(X_l^-1)
	 *              Y_l = - ------------------------
	 *                       X_l^-1 lambda'(X_l^-1)
	 *
	 * see Blahut section 7.5
	 *
	 * num = omega(X_l^-1) * (X_l^-1)^(J0-1) (huh?) and
	 * den = lambda'(X(l)^-1).
	 */

	/* this is now the degree of lambda'(x) */
	deg_lambda = min(deg_lambda, rs_format->parity-1) & ~1;

	for(y = &root[count - 1]; y >= root; y--)
		{
		/* lambda[i+1] for i even is the formal derivative lambda' of lambda[i] */
		den = 0;
		for(x = &lambda[deg_lambda + 1], tmp = deg_lambda * *y; x >= lambda; tmp -= *y << 1, x -= 2)
			if(*x != INFINITY)
				den ^= Alpha_exp[modNN(*x + tmp)];
		if(den == 0)
			return(-1);
    
		num = 0;
		for(i = tmp = 0; i <= deg_omega; i++)
			{
			if(omega[i] != INFINITY)
				num ^= Alpha_exp[omega[i] + tmp];
			if((tmp += *y) >= NN)
				tmp -= NN;
			}
		if(num == 0)
			continue;
		num = modNN(Log_alpha[num] + (J0-1) * *y);

		/* Apply error to block */
		block[loc[y - root]] ^= Alpha_exp[num + NN - Log_alpha[den]];
		}

	if(erasure != NULL)
		memcpy(erasure, loc, count * sizeof(gf));
	return(count);
}


/*
 * Encoder/decoder initialization --- call this first!
 *
 * If you want to select your own field generator polynomial, edit that
 * here.  I've chosen to put the numbers in octal format because I find it
 * easier to see the bit patterns than with either hexadecimal or decimal
 * formats.  Symbol sizes larger than 16 bits begin to require
 * prohibitively large multiplication look-up tables.  If additional field
 * generators are added be sure to edit the range check for MM in rs.h
 */

int reed_solomon_init(unsigned int n, unsigned int k, struct rs_format_t *rs_format)
{
	int p =
#if   (MM == 2)
	        0000007;        /* x^2 + x + 1               */
#elif (MM == 3)
	        0000013;        /* x^3 + x + 1               */
#elif (MM == 4)
	        0000023;        /* x^4 + x + 1               */
#elif (MM == 5)
	        0000045;        /* x^5 + x^2 + 1             */
#elif (MM == 6)
	        0000103;        /* x^6 + x + 1               */
#elif (MM == 7)
	        0000211;        /* x^7 + x^3 + 1             */
#elif (MM == 8)
	        0000435;        /* x^8 + x^4 + x^3 + x^2 + 1 */
#elif (MM == 9)
	        0001021;        /* x^9 + x^4 + 1             */
#elif (MM == 10)
	        0002011;        /* x^10 + x^3 + 1            */
#elif (MM == 11)
	        0004005;        /* x^11 + x^2 + 1            */
#elif (MM == 12)
	        0010123;        /* x^12 + x^6 + x^4 + x + 1  */
#elif (MM == 13)
	        0020033;        /* x^13 + x^4 + x^3 + x + 1  */
#elif (MM == 14)
	        0042103;        /* x^14 + x^10 + x^6 + x + 1 */
#elif (MM == 15)
	        0100003;        /* x^15 + x + 1              */
#elif (MM == 16)
	        0210013;        /* x^16 + x^12 + x^3 + x + 1 */
#endif

	if((n > NN) || (k >= n) || (n-k > MAX_PARITY))
		return(-1);
	rs_format->n = n;
	rs_format->k = k;
	rs_format->parity = n - k;
	rs_format->remainder_start = (rs_format->n - 1) % rs_format->parity;

	generate_GF(p);
	generate_poly(rs_format);

	#if LOG_BETA != 1
	gen_ldec();
	#endif

	return(0);
}
