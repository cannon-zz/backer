/*
 * Reed-Solomon coding and decoding
 *
 * This code is an overhaul of a Reed-Solomon encoder/decoder implementation
 * written by Phil Karn which was distributed under the name rs-2.0.
 *
 * Copyright (C) 2000 Kipp C. Cannon
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
 * Consultative Committe for Space Data Systems, Telemtry Channel Coding
 *	(CCSDS 101.0-B-4), May 1999.
 */

/*
 * To use this in the Linux kernel different header choices need to be made.
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif /* __KERNEL__ */

#include "rs.h"


/*
 * Compute x % NN, where NN is 2^MM-1, without a slow divide.
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

#define	min(a,b)  ((a) < (b) ? (a) : (b))


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
 * Constructs look-up tables for performing arithmetic over the Galois Field
 * GF(2^MM) from the irreducible polynomial p(x) stored in p.
 *
 * We can intuitively define the addition and multiplication operations for
 * the field using the "polynomial" representation for the elements of
 * GF(2^MM).  In this representation, each element is taken to be an MM-
 * component vector whose components are elements from GF(2) (i.e. 0 and 1).
 * We interpret the components of our vectors as coefficients of an MM-1 order
 * polynomial.  Addition and multiplication are then performed using the
 * standard rules for adding and multiplying polynomials but with the
 * operations being those from GF(2) and with the result of multiplication
 * being taken modulo an irreducible polynomial of order MM over GF(2) to
 * ensure the order of the result does not exceed MM-1.
 *
 * Using this definition for the arithmetic operations and using MM-bit words
 * to store the components of our vectors, addition is simply an XOR operation
 * (making it identical to its inverse operation, subtraction).
 *
 * Multiplication, however, has no such trivial representation using machine
 * operations and must be performed using look-up tables.  We do this by
 * translating to "alpha" representation.  Alpha is the name commonly given to
 * the element of the field that is the root of the polynomial used to
 * construct the field --- it's the only root since p(x) is irreducible.  It
 * can be shown that all elements (except, of course, 0) can be represented as
 * powers of alpha.  We can perform multiplication, then, by taking the log
 * (base alpha) of two elements, adding the result modulo 2^MM-1, then
 * looking up the polynomial representation of that power of alpha to get the
 * answer.
 *
 * This function constructs the necessary look-up tables called Log_alpha[]
 * and Alpha_exp[] from p.  Since 0 cannot be represented as a power of alpha,
 * special elements are added to the look-up tables to represent this case
 * and all multiplications must first check for this.  We call this special
 * element INFINITY since it is the result of taking the log (base alpha) of
 * zero.
 *
 * The bits of the parameter p specify the generator polynomial with the LSB
 * being the co-efficient of x^0 and the MSB being the coefficient of x^MM.
 *
 *          p(x) = p_MM * x^MM + ... + p_1 * x^1 + p_0 * x^0
 *
 * where p_i means the i-th bit in p and p_MM must = 1.
 *
 * For field generator polynomials, see Lin & Costello, Appendix A, and Lee
 * and Messerschmitt, pg. 453.  Field generator polynomials for symbol sizes
 * from 2 to 16 bits can be found in reed_solomon_init() below.
 */

static gf  Alpha_exp[NN + 1];   /* exponent->polynomial conversion table */
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

	Alpha_exp[INFINITY] = 0;
	Log_alpha[0] = INFINITY;
}


/*
 * generate_poly()
 *
 * Constructs the generator polynomial for the Reed-Solomon code of length NN
 * (=2^MM-1) with the number of parity symbols being given by rs_format->parity.
 *
 * The generator polynomial, g(x), has coefficients taken from GF(2^MM) and is
 * given by the product
 *
 *  g(x) = (x - beta^J0) * (x - beta^(J0+1)) * ... * (x - beta^(J0+2t-1))
 *
 * where beta = alpha^LOG_BETA and 2t = rs_format->parity.  Normally
 * beta = alpha and J0 = 1 so the product is more simply written as
 *
 *     g(x) = (x - alpha) * (x - alpha^2) * ... * (x - alpha^(2t))
 *
 * but some codes use different choices.  For instance the Reed-Solomon code
 * suggested for use on spacecraft telemetry channels by the Consultative
 * Committe for Space Data Systems (CCSDS) uses beta=alpha^11 and uses beta^112
 * through beta^143 as the roots of the generator polynomial so LOG_BETA=11,
 * J0=112.  (with n-k=32, these choices produce a g(x) which is symmetric under
 * a lowest order to highest order mirror of its coefficients and presumably one
 * can use this fact to reduce the computations needed to encode data).
 *
 * The co-efficients of the generator polynomial are left in exponent form for
 * better encoder performance but note that it is still faster to compute them
 * in polynomial form and convert them afterwards.
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
 * A reed solomon code consists of the set of polynomials which are all
 * multiples of a given polynomial called the generator polynomial, g(x).
 * The generator polynomial is chosen so as to maximize the distance
 * between code polynomials.  There are many ways to map data polynomials to
 * the set of code polynomials and the "systematic" method is used here.  In
 * this method, the code polynomial is given by
 *
 *                   c(x) = d(x)*x^(n-k) + b(x)
 *
 * where d(x) are the data symbols (as the co-efficients of a k-th degree
 * polynomial) and b(x) are the parity symbols.  The data appears as the high
 * order terms of the code polynomial so no processing is needed to extract
 * the data in the decoder (beyond checking and correcting).  The parity
 * polynomial is simply the remainder d(x)*x^(n-k) mod g(x) so that when
 * added to d(x)*x^(n-k) the result is an exact multiple of g(x) and thus
 * a code polynomial.
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
 * so the parity symbols appear at the bottom of the array.  One must leave
 * enough room for them by having the data start at offset (n-k) in the array.
 * A side benefit of this is that it reminds the programmer that the array must
 * be allocated large enough to hold not only their data but also the parity
 * symbols.
 *
 * A reduced Reed-Solomon code word is generated by setting the correct number
 * of high order symbols to zero.  This reduces the operations needed to
 * compute the remainder thus increasing the speed at which reduced
 * block sizes are handled.  These zeroes are implicit in the encoder and
 * decoder so the data array need only be large enough to contain the desired
 * data and parity symbols.
 *
 * The encoding algorithm is the long division algorithm but where we only
 * care about the remainder.  A shift register is used to keep track of the
 * remainder at each step and the algorithm is as follows (recall that all
 * operations are performed over GF(2^MM) so addition is the same as
 * subtraction):
 *   1.  Initialise shift register to 0.
 *   2.  Starting from the highest order data symbol...
 *   3.  Add the highest element of the shift register to the current
 *       data symbol to compute a feed-back multiplier.
 *   4.  Shift the register one to the left so 0 -> b0, b0 -> b1, etc.
 *   5.  Multiply each co-efficient of the generator polynomial by
 *       the feedback and add the resulting polynomial to the shift
 *       register.
 *   6.  Get the next data symbol and goto step 3 until all data symbols
 *       have been processed.
 *   7.  The contents of the shift register are the parity symbols.
 */

void reed_solomon_encode(dtype *block, struct rs_format_t *rs_format)
{
	int    i, j;                    /* general purpose counters */
	int    b;                       /* feed-back index */
	gf     feedback;                /* feed-back multiplier */

	memset(block, 0, rs_format->parity * sizeof(dtype));

	/*
	 * At the start of each iteration of the loop, b is the index of the
	 * shift register used to form the feed-back.
	 */

	b = (rs_format->n - 1) % rs_format->parity;

	for(i = rs_format->n-1; i >= rs_format->parity; i--)
		{
		feedback = Log_alpha[block[i] ^ block[rs_format->parity-1]];
		if(feedback != INFINITY)
			{
			for(j = rs_format->parity-1; j > 0; j--)
				if(rs_format->g[j] != INFINITY)
					block[j] = block[j-1] ^ Alpha_exp[modNN(feedback + rs_format->g[j])];
			block[0] = Alpha_exp[modNN(feedback + rs_format->g[0])];
			}
		else
			{
			for(j = rs_format->parity-1; j > 0; j--)
				block[j] = block[j-1];
			block[0] = 0;
			}

		/*
		 * Argh:  by all rights the following should be faster than the preceding but
		 * time trials show it to be 5% slower on my PII-400.  I must be missing
		 * some sort of hardware exploit...
		 *
		feedback = Log_alpha[block[i] ^ block[b]];
		if(feedback != INFINITY)
			{
			block[b--] = Alpha_exp[modNN(feedback + rs_format->g[0])];
			for(j = rs_format->parity-1; b >= 0; j--, b--)
				if(rs_format->g[j] != INFINITY)
					block[b] ^= Alpha_exp[modNN(feedback + rs_format->g[j])];
			for(b = rs_format->parity-1; j > 0; b--, j--)
				if(rs_format->g[j] != INFINITY)
					block[b] ^= Alpha_exp[modNN(feedback + rs_format->g[j])];
			}
		else
			block[b] = 0;
		if(--b < 0)
			b = rs_format->parity - 1;
		*/
		}
}


/*
 * reed_solomon_decode()
 *
 * Decodes the received vector.  If decoding is successful then the corrected
 * code word is written to block[] otherwise the received vector is left
 * unmodified.  The number of symbols corrected is returned or -1 if the code
 * word cannot be corrected.
 *
 * There are two types of corrupted symbol.  An incorrect symbol in the vector
 * is called an "error".  If, for some reason, the location of the incorrect
 * symbol is known then it is instead called an "erasure".  A Reed-Solomon code
 * with n-k parity symbols can correct up to and including n-k erasures or
 * (n-k)/2 errors or any combination thereof with each error counting as two
 * erasures.  The number of corrupt bits within a corrupt symbol is
 * irrelevant:  the symbol is either bad or good.
 *
 * If erasures are known, then their locations are passed to the decoder in
 * the erasure[] array and the number of them is passed in the no_eras
 * parameter.  NOTE:  there must not be duplicates in this array.  If none are
 * known then simply pass NULL for erasure[] and 0 for no_eras.
 *
 * Whether erasures are known or not, if erasure[] is not equal to NULL then
 * the locations of all bad symbols will be written to this array so it must
 * be large enough to store them.
 *
 * The bulk of this code has been taken from Phil Karn's decoder implementation
 * but with many minor modifications to things like the directions of for
 * loops, their bounds, etc. all with an eye to performance.  The fundamental
 * algorithm itself, however, has not been changed.  For performance reasons
 * the algorithm is fairly complicated.  In principle it is nothing more than
 * the inversion of an (n-k) x (n-k) matrix but that is an order n^3 operation
 * and one can avoid much of the computations by exploiting symmetries in the
 * matrix.  The algorithms used here are the Berlekamp-Massey algorithm for
 * finding the error locations and the Forney algorithm for finding the error
 * magnitudes.
 */

int reed_solomon_decode(dtype *block, gf *erasure, int no_eras, struct rs_format_t *rs_format)
{
	int  i, j, k;                   /* general purpose loop indecies */
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

	/*
	 * Compute the syndromes by evaluating block(x) at the roots of g(x),
	 * namely beta^(J0+i), i = 0, ... ,(n-k-1).  When finished, convert
	 * them to alpha-rep and test for all zero.  If the syndromes are
	 * all zero, block[] is a codeword and there are no errors to correct.
	 */

	for(i = rs_format->parity; i; s[--i] = block[0]);

	for(j = 1; j < rs_format->n; j++)
		if(block[j] != 0)
			{
			tmp = modNN(Log_alpha[block[j]] + LOG_BETA*J0*j);
			for(i = 0; i < rs_format->parity; i++)
				{
				s[i] ^= Alpha_exp[tmp];
				#if (LOG_BETA==1)
				if((tmp += j) >= NN)
					tmp -= NN;
				#else
				tmp = modNN(tmp + LOG_BETA*j);
				#endif /* LOG_BETA */
				}
			}
	for(i = rs_format->parity-1; !s[i]; )
		{
		s[i] = INFINITY;
		if(--i < 0)
			goto finish;
		}
	for(; i >= 0; i--)
		s[i] = Log_alpha[s[i]];

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
			for(j = i+1; j > 0; j--)
				if(lambda[j-1] != 0)
					lambda[j] ^= Alpha_exp[modNN(tmp + Log_alpha[lambda[j-1]])];
			}
		}
	deg_lambda = no_eras;

	/*
	 * Berlekamp-Massey algorithm to determine error+erasure locator
	 * polynomial.  See Blahut, R., Theory and Practice of Error Control
	 * Codes, section 7.5.
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
				discr ^= Alpha_exp[modNN(Log_alpha[lambda[i]] + s[j - i])];

		discr = Log_alpha[discr];

		if(discr == INFINITY)
			{
			/* b(x) <-- x*b(x) */
			memmove(&b[1], b, rs_format->parity * sizeof(gf));
			b[0] = INFINITY;
			continue;
			}

		/* temp(x) <-- lambda(x) - discr*x*b(x) */
		for(i = rs_format->parity; i >= 1; i--)
			{
			if(b[i-1] != INFINITY)
				temp[i] = lambda[i] ^ Alpha_exp[modNN(discr + b[i-1])];
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
			memmove(&b[1], b, rs_format->parity * sizeof(gf));
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
	 * Find roots of the error & erasure locator polynomial by Chien search
	 * (i.e. trial and error).  At each iteration, i, the j-th component of
	 * temp[] contains the j-th coefficient of lambda multiplied by
	 * alpha^(i*j).  We get components of temp[] for the next iteration by
	 * multiplying each by alpha^j.  Note that the 0-th coefficient of
	 * lambda is always 1 so we can handle it explicitly.
	 */

	memcpy(&temp[1], &lambda[1], deg_lambda * sizeof(gf));

	for(i = 1, k = NN-Ldec; i <= NN; i++, k = modNN(NN - Ldec + k))
		{
		tmp = 1;
		for(j = deg_lambda; j > 0; j--)
			if(temp[j] != INFINITY)
				{
				temp[j] = modNN(temp[j] + j);
				tmp ^= Alpha_exp[temp[j]];
				}

		if(tmp != 0)
			continue;

		/*
		 * Store root and error location number.  If we've found as many
		 * roots as lambda(x) has then abort to save time.
		 */

		root[count] = i;
		loc[count] = k;
		if(loc[count] >= rs_format->n)
			{
			count = -1;
			goto finish;
			}
		if(++count == deg_lambda)
			break;
		}

	/*
	 * deg(lambda) != number of roots --> uncorrectable error detected
	 */

	if(deg_lambda != count)
		{
		count = -1;
		goto finish;
		}

	/*
	 * Compute error & erasure evaluator poly omega(x) = s(x)*lambda(x)
	 * (modulo x^(n-k)) in alpha rep.  Also find deg(omega).
	 */

	for(i = deg_omega = 0; i < rs_format->parity; i++)
		{
		omega[i] = 0;
		for(j = min(deg_lambda, i); j >= 0; j--)
			if((s[i - j] != INFINITY) && (lambda[j] != INFINITY))
				omega[i] ^= Alpha_exp[modNN(s[i - j] + lambda[j])];
		if(omega[i] != 0)
			deg_omega = i;
		omega[i] = Log_alpha[omega[i]];
		}
  
	/*
	 * Forney algorithm for computing error magnitudes.  If X_l^-1 is the
	 * l-th root of lambda (X_l is the l-th error location) then Y_l, the
	 * magnitude of the l-th error, is given  by
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

	for(j = count-1; j >= 0; j--)
		{
		/* lambda[i+1] for i even is the formal derivative lambda' of lambda[i] */
		den = 0;
		for(i = min(deg_lambda, rs_format->parity-1) & ~1; i >= 0; i -= 2)
			if(lambda[i+1] != INFINITY)
				den ^= Alpha_exp[modNN(lambda[i+1] + i * root[j])];
		if(den == 0)
			{
			count = -1;
			goto finish;
			}
    
		num = 0;
		for(i = deg_omega; i >= 0; i--)
			if(omega[i] != INFINITY)
				num ^= Alpha_exp[modNN(omega[i] + i * root[j])];
		if(num == 0)
			continue;
		num = Log_alpha[num] + root[j] * (J0-1);

		/* Apply error to block */
		block[loc[j]] ^= Alpha_exp[modNN(num + NN - Log_alpha[den])];
		}

	if(erasure != NULL)
		memcpy(erasure, loc, count * sizeof(gf));

	finish:
	return(count);
}


/*
 * Encoder/decoder initialization --- call this first!
 *
 * If you want to select your own field generator polynomial, edit that here.
 * I've chosen to put the numbers in octal format because I find it easier to
 * see the bit patterns than with either hexadecimal or decimal formats.
 * Symbol sizes larger than 16 bits begin to require prohibitively large
 * multiplication look-up tables.  If additional field generators are added
 * be sure to edit the range check for MM in rs.h
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

	generate_GF(p);
	generate_poly(rs_format);

	#if LOG_BETA != 1
	gen_ldec();
	#endif

	return(0);
}
