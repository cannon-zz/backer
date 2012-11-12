/*
 * Reed-Solomon coding and decoding
 *
 * This code is an overhaul of a Reed-Solomon encoder/decoder implementation
 * written by Phil Karn which was distributed under the name rs-2.0.
 *
 * Copyright (C) 2000,2001,2002 Kipp C. Cannon, kcannon@sourceforge.net
 * Portions Copyright (C) 1999 Phil Karn, karn@ka9q.ampr.org
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

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
static void *malloc(size_t size)  { return kmalloc(size, GFP_KERNEL); }
static void free(void *ptr) { kfree(ptr); }

#else

#include <stdlib.h>
#include <string.h>

#endif /* __KERNEL__ */

#include <rs.h>


/*
 * NN is the number of non-zero symbols in the Galois Field.  This is also
 * the maximum number of symbols which can be used to form a code word
 * (Reed-Solomon block).
 */

#define	NN  ((1 << MM) - 1)


/*
 * Return the smaller/larger of two values.
 */

#ifndef min
#define min(x,y) ({ \
	const typeof(x) _x = (x); \
	const typeof(y) _y = (y); \
	(void) (&_x == &_y); \
	_x < _y ? _x : _y; \
})
#endif

#ifndef max
#define max(x,y) ({ \
	const typeof(x) _x = (x); \
	const typeof(y) _y = (y); \
	(void) (&_x == &_y); \
	_x > _y ? _x : _y; \
})
#endif


/*
 * Compute x % NN, where NN is 2^MM-1, without a divide.
 */

static gf modNN(int x)
{
	while(x >= NN) {
		x -= NN;
		x = (x >> MM) + (x & NN);
	}
	return x;
}


/*
 * galois_field_init()
 *
 * Call this first.
 */

static gf  Alpha_exp[2*NN];     /* exponent->polynomial conversion table */
static gf  Log_alpha[NN + 1];   /* polynomial->exponent conversion table */
#define    INFINITY  (NN)       /* representation of 0 in exponent form */

void galois_field_init(int p)
{
	int i;

	for(i = 0; i < MM; i++)
		Alpha_exp[i] = 1 << i;

	Alpha_exp[MM] = p & NN;

	for(i++; i < NN; i++) {
		Alpha_exp[i] = Alpha_exp[i-1] << 1;
		if(Alpha_exp[i-1] & (1 << (MM-1)))
			Alpha_exp[i] = (Alpha_exp[i] & NN) ^ Alpha_exp[MM];
	}

	memcpy(&Alpha_exp[NN], Alpha_exp, NN * sizeof(*Alpha_exp));

	for(i = 0; i < NN; i++)
		Log_alpha[Alpha_exp[i]] = i;
	Log_alpha[0] = INFINITY;

}


/*
 * polynomial_assign()
 *
 * Copy the coefficients of src(x), having degree deg, to dst(x).
 */

static void polynomial_assign(gf *dst, const gf *src, int deg)
{
	memcpy(dst, src, (deg + 1) * sizeof(*dst));
}


/*
 * polynomial_to_alpha()
 *
 * Convert a polynomial to alpha-rep and determine its degree.
 */

static int polynomial_to_alpha(gf *poly, int deg)
{
	gf  *x;

	for(x = &poly[deg]; !*x && (x >= poly); x--)
		*x = INFINITY;
	for(deg = x - poly; x >= poly; x--)
		*x = Log_alpha[*x];

	return deg;
}


/*
 * polynomial_evaluate()
 *
 * Evaluate poly(x).
 */

static gf polynomial_evaluate(const gf *poly, int deg, gf log_x)
{
	gf  result;

	for(result = 0; deg >= 0; deg--) {
		if(result)
			result = Alpha_exp[Log_alpha[result] + log_x];
		result ^= poly[deg];
	}

	return result;
}

static gf log_polynomial_evaluate(const gf *log_poly, int deg, gf log_x)
{
	gf  result, n_log_x;

	for(result = n_log_x = 0; deg >= 0; log_poly++, deg--) {
		if(*log_poly != INFINITY)
			result ^= Alpha_exp[*log_poly + n_log_x];
		n_log_x = modNN(n_log_x + log_x);
	}

	return result;
}


/*
 * polynomial_differentiate()
 *
 * Replace a polynomial with its derivative.
 */

static int polynomial_differentiate(gf *poly, int deg)
{
	int  i;

	memmove(poly, poly+1, deg * sizeof(*poly));
	for(poly++, i = 0; i < deg; poly += 2, i += 2)
		*poly = INFINITY;

	return --deg & ~1;
}


/*
 * polynomial_multiply()
 *
 * Multiply two polynomials (in alpha-rep modulo x^deg_dst).  Return
 * the degree of the result.
 */

static int polynomial_multiply(gf *dst, int deg_dst, const gf *src1, int deg_src1, const gf *src2, int deg_src2)
{
	int  i, j;

	memset(dst, 0, (deg_dst + 1) * sizeof(*dst));
	src2 += deg_src2;
	for(i = deg_src2; i >= 0; src2--, i--)
		if(*src2 != INFINITY)
			for(j = min(deg_src1 + i, deg_dst); j >= i; j--)
				if(src1[j - i] != INFINITY)
					dst[j] ^= Alpha_exp[*src2 + src1[j - i]];

	while(!dst[deg_dst])
		deg_dst--;
	return deg_dst;
}


/*
 * Set LOG_BETA to the power of alpha used to generate the roots of the
 * generator polynomial and set J0 to the power of beta to be used as the
 * first root.  The generator polynomial will then have roots
 *
 *    beta^J0, beta^(J0+1), beta^(J0+2), ..., beta^(J0+2t-1)
 *
 * where beta = alpha^LOG_BETA and 2t is the number of parity symbols in
 * the code.  A conventional Reed-Solomon encoder is selected by setting J0
 * = 1, LOG_BETA = 1.  NOTE:  LOG_BETA and NN must be relatively prime i.e.
 * for 8-bit symbols (NN = 255), LOG_BETA cannot be a multiple of 3, 5 or
 * 17.
 */

#define J0        1
#define LOG_BETA  1


/*
 * generator_polynomial_init()
 *
 * Constructs the generator polynomial for the Reed-Solomon code of length
 * NN (=2^MM-1) with the number of parity symbols being given by
 * rs_format->parity.
 */

static void generator_polynomial_init(gf *g, gf *log_beta, int num_parity)
{
	int  i, j;
	gf  log_factor;

	g[0] = 1;

	for(i = 0; i < num_parity; i++) {
		log_factor = modNN(LOG_BETA*(J0+i));
		g[i+1] = 1;
		for(j = i; j > 0; j--) {
			if(g[j])
				g[j] = g[j-1] ^ Alpha_exp[Log_alpha[g[j]] + log_factor];
			else
				g[j] = g[j-1];
		}
		g[0] = Alpha_exp[Log_alpha[g[0]] + log_factor];
	}

	polynomial_to_alpha(g, num_parity);

	/*
	 * Construct the beta log look-up table.  The i-th element of this
	 * table gives the power to which beta must be raised to equal
	 * alpha^i.  i.e. beta^(log_beta[i]) == alpha^i.
	 *
	 * Note:  an infinite loop will result if LOG_BETA and NN are not
	 * relatively prime.
	 */

	for(i = 0; i < NN; i++)
		for(log_beta[i] = 0; modNN(LOG_BETA * log_beta[i]) != i; log_beta[i]++);
	log_beta[INFINITY] = INFINITY;
}


/*
 * Encoder/decoder instance creation
 *
 * Call this second.
 *
 * If you want to select your own field generator polynomial, edit that
 * here.  I've chosen to put the numbers in octal format because I find it
 * easier to see the bit patterns than with either hexadecimal or decimal
 * formats.  Symbol sizes larger than 16 bits begin to require
 * prohibitively large multiplication look-up tables.  If additional field
 * generators are added be sure to edit the range check for MM.
 */

rs_format_t *reed_solomon_codec_new(unsigned int n, unsigned int k, int interleave)
{
	rs_format_t *format = malloc(sizeof(*format));

	if((n > NN) || (k > n) || (interleave < 1) || !format) {
		free(format);
		return NULL;
	}

	format->n = n;
	format->k = k;
	format->parity = n - k;
	if(format->parity)
		format->remainder_start = (format->n - 1) % format->parity;
	else
		format->remainder_start = 0;
	format->interleave = interleave;

	format->g = malloc((format->parity+1) * sizeof(*format->g));
	format->erasure = malloc(format->parity * sizeof(*format->erasure));
	format->log_beta = malloc((NN + 1) * sizeof(*format->log_beta));

	if(!format->g || !format->erasure || !format->log_beta) {
		reed_solomon_codec_free(format);
		return NULL;
	}

	generator_polynomial_init(format->g, format->log_beta, format->parity);

	return format;
}


/*
 * Encoder/decoder clean-up --- call this when done!
 */

void reed_solomon_codec_free(rs_format_t *format)
{
	if(format) {
		free(format->g);
		free(format->erasure);
		free(format->log_beta);
	}
	free(format);
}


/*
 * reed_solomon_encode()
 *
 * In order to use this encoder (and the decoder that follows) with other
 * software, the following information on the conventions used in this
 * software will be required.  The parity symbols and data symbols taken
 * together form a single logical code word with the parity symbols
 * appearing below the data symbols.  The code symbols that are returned
 * from this encoder are given with the lowest order term at offset 0 in
 * the array.  So if c(x) is the resultant Reed-Solomon code polynomial,
 * i.e.
 *
 *    c(x) = c_NN * x^NN + c_(NN-1) * x^(NN-1) + ... + c_0 * x^0,
 *
 * and block[] is a hypothetical array with NN+1 elements then c(x) is
 * stored in block[] as
 *
 *    block[] = { c_0, ..., c_NN }
 *
 * and block[] is mapped into the parity[] and data[] arrays as follows
 *
 *    block[0]                  = parity[0]
 *    block[1]                  = parity[1]
 *      ...                          ...
 *    block[format->parity - 1] = parity[format->parity - 1]
 *    block[format->parity    ] = data[0]
 *    block[format->parity + 1] = data[1]
 *      ...                         ...
 *    block[format->n - 1     ] = data[format->k - 1]
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

void reed_solomon_encode(rs_symbol_t *parity, const rs_symbol_t *data, rs_format_t format)
{
	const rs_symbol_t  *d;  /* current data symbol */
	rs_symbol_t  *b;        /* current remainder symbol */
	gf  *g;                 /* current gen. poly. symbol */
	gf  feedback;           /* feed-back multiplier */
	rs_symbol_t  remainder[format.parity];

	if(!format.parity)
		return;

	memset(remainder, 0, format.parity);

	/*
	 * At the start of each iteration, b points to the most significant
	 * symbol in the remainder for that iteration.
	 */

	b = &remainder[format.remainder_start];

	for(d = &data[(format.k - 1)*format.interleave]; d >= data; d -= format.interleave) {
		feedback = Log_alpha[*d ^ *b];
		if(feedback != INFINITY) {
			b--;
			for(g = &format.g[format.parity - 1]; b >= remainder; g--, b--)
				if(*g != INFINITY)
					*b ^= Alpha_exp[feedback + *g];
			for(b = &remainder[format.parity - 1]; g > format.g; b--, g--)
				if(*g != INFINITY)
					*b ^= Alpha_exp[feedback + *g];
			*b = Alpha_exp[feedback + *g];
		} else
			*b = 0;
		if(--b < remainder)
			b = &remainder[format.parity - 1];
	}

	/*
	 * Copy parity symbols from remainder[] to parity[].
	 */

	for(feedback = 0; feedback < format.parity; parity += format.interleave)
		*parity = remainder[feedback++];
}


/*
 * reed_solomon_decode()
 *
 * The algorithms used here are the Berlekamp-Massey algorithm for finding
 * the error locations and the Forney algorithm for finding the error
 * magnitudes.  The implementations are a hybrid of time-domain and
 * frequency-domain.
 */

static int compute_syndromes(gf *s, const rs_symbol_t *parity, const rs_symbol_t *data, int n, int num_parity, int interleave)
{
	int  i;
	gf  block[n--];

	/*
	 * Compute the syndromes by evaluating block(x) at the roots of
	 * g(x), namely beta^(J0+i), i = 0, ... ,(n-k-1).  When finished,
	 * convert them to alpha-rep.
	 */

	for(i = 0; i < num_parity; parity += interleave, i++)
		block[i] = *parity;
	for(; i <= n; data += interleave, i++)
		block[i] = *data;

	for(i = 0; i < num_parity; i++)
		s[i] = polynomial_evaluate(block, n, modNN(LOG_BETA*(J0+i)));

	return num_parity ? polynomial_to_alpha(s, num_parity - 1) : -1;
}


static gf compute_discrepancy(int r, const gf *lambda, int deg_lambda, const gf *s, int deg_s)
{
	int  i = max(r - deg_s, 0);
	gf  discrepancy = 0;

	/*
	 * Compute the Berlekamp-Massey discrepancy for the (r-1)-th step
	 * number.  This is the convolution of lambda(x) with the
	 * syndromes.  lambda(x) is assumed to be in poly-rep while the
	 * syndromes are in alpha-rep.
	 */

	for(lambda = &lambda[i], s = &s[r - i]; i <= deg_lambda; lambda++, s--, i++)
		if(*lambda && (*s != INFINITY))
			discrepancy ^= Alpha_exp[Log_alpha[*lambda] + *s];

	return discrepancy;
}


static void add_poly_times_const(gf *dst, const gf *src, int deg, gf constant)
{
	/*
	 * Compute dst(x) = dst(x) + constant * src(x).  Recall that
	 * arithmetic is being performed over GF(2^n) so addition is the
	 * same as subtraction i.e. we are also computing dst(x) = dst(x) -
	 * constant * src(x).
	 */

	for(; deg >= 0; src++, dst++, deg--)
		if(*src)
			*dst ^= Alpha_exp[Log_alpha[*src] + constant];
}


static int compute_lambda(gf *lambda, int num_parity, const gf *erasure, int num_erase, const gf *s, int deg_s)
{
	int  deg_lambda;        /* degree of lambda */
	gf  *x;                 /* general purpose loop pointer */
	int  r;                 /* step number - 1 */
	gf  log_loc_num;        /* log (base alpha) of current location number */
	gf  discr;              /* discrepancy */
	gf  b[num_parity+1];    /* shift register */

	/*
	 * Initialize lambda to be the erasure locator polynomial
	 *
	 * lambda(x) = (1 - x X1)*(1 - x X2)*...*(1 - x Xn)
	 *
	 * where Xi is the i-th location number = beta^(i-th location).
	 */

	lambda[0] = 1;
	memset(&lambda[1], 0, num_parity * sizeof(*lambda));

	for(deg_lambda = 0; deg_lambda < num_erase; deg_lambda++) {
		log_loc_num = modNN(LOG_BETA * *(erasure++));
		for(x = &lambda[deg_lambda+1]; x > lambda; x--)
			if(*(x-1))
				*x ^= Alpha_exp[log_loc_num + Log_alpha[*(x-1)]];
	}

	/*
	 * Berlekamp-Massey algorithm to determine error+erasure locator
	 * polynomial.  See Blahut, R., Theory and Practice of Error
	 * Control Codes, sections 7.5, 9.1 and 9.2.
	 */

	polynomial_assign(b, lambda, num_parity);

	for(r = num_erase; r < num_parity; r++) {
		/* b(x) <-- x*b(x) */
		memmove(&b[1], &b[0], num_parity * sizeof(*b));
		b[0] = 0;

		/* compute discrepancy */
		discr = compute_discrepancy(r, lambda, deg_lambda, s, deg_s);
		if(!discr)
			continue;
		discr = Log_alpha[discr];

		/* lambda(x) <-- lambda(x) - discr*b(x) */
		add_poly_times_const(lambda, b, num_parity, discr);

		if(2 * deg_lambda > r + num_erase)
			continue;
		deg_lambda = r+1 + num_erase - deg_lambda;

		/* b(x) <-- b(x) + lambda(x)/discr */
		add_poly_times_const(b, lambda, deg_lambda, NN - discr);
	}

	return polynomial_to_alpha(lambda, num_parity);
}

static int find_roots(gf *lambda, int deg_lambda, gf *log_root, gf *erasure, const gf *log_beta)
{
	gf  temp[deg_lambda+1];
	gf  *x;
	int  i, j;
	gf  result;
	int  num_roots = 0;

	/*
	 * Find roots of the error & erasure locator polynomial by Chien
	 * search (i.e. trial and error).  At each iteration, i, the j-th
	 * component of temp[] contains the j-th coefficient of lambda
	 * multiplied by alpha^(i*j).  We get components of temp[] for the
	 * next iteration by multiplying each by alpha^j.
	 */

	polynomial_assign(temp + 1, lambda + 1, deg_lambda - 1);

	for(i = 1; i <= NN; i++) {
		result = 1;	/* 0-th order term is always = 1 */
		for(j = deg_lambda, x = &temp[deg_lambda]; j > 0; x--, j--)
			if(*x != INFINITY)
				result ^= Alpha_exp[*x = modNN(*x + j)];
		if(result)	/* not a root */
			continue;

		/*
		 * Store root
		 */

		*(log_root++) = i;

		/*
		 * Store corresponding error location:
		 *
		 * root^-1 = beta^(ith location)
		 */

		*(erasure++) = log_beta[NN - i];

		/*
		 * Found as many roots as lambda(x) has?
		 */

		if(++num_roots >= deg_lambda)
			break;
	}

	return num_roots;
}


int reed_solomon_decode(rs_symbol_t *parity, rs_symbol_t *data, int num_erase, rs_format_t format)
{
	int  i;                 /* general purpose loop pointer */
	gf  s[format.parity];   /* syndromes */
	int  deg_s;             /* number of syndromes - 1 */
	gf  lambda[format.parity+1];  /* error & erasure locator polynomial */
	int  deg_lambda;        /* degree of lambda(x) */
	gf  omega[format.parity];  /* error & erasure evaluator polynomial */
	int  deg_omega;         /* degree of omega(x) */
	gf  root[format.parity];   /* roots of lambda */
	int  num_roots;         /* number of roots of lambda */
	gf  magnitude;          /* error magnitude from Forney algorithm */
	gf  denominator;        /* denominator for Forney algorithm */


	deg_s = compute_syndromes(s, parity, data, format.n, format.parity, format.interleave);
	if(deg_s < 0)
		return 0;	/* all syndromes are 0 == code word is valid */

	deg_lambda = compute_lambda(lambda, format.parity, format.erasure, num_erase, s, deg_s);

	num_roots = find_roots(lambda, deg_lambda, root, format.erasure, format.log_beta);
	if(num_roots != deg_lambda)
		return -RS_EDEGENERATEROOTS;	/* lambda(x) has degenerate roots == code word is uncorrectable */

	/*
	 * Compute error & erasure evaluator polynomial:
	 * omega(x) = s(x)*lambda(x)
	 */

	deg_omega = polynomial_multiply(omega, format.parity - 1, s, deg_s, lambda, deg_lambda);

	/*
	 * after this, lambda[] holds lambda'(x)
	 */

	deg_lambda = polynomial_differentiate(lambda, deg_lambda);

  
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
	 * numerator = omega(X_l^-1) * (X_l^-1)^(J0-1) and  <--- ** huh? **
	 * denominator = lambda'(X(l)^-1).
	 */

	for(i = 0; i < num_roots; i++) {
		magnitude = polynomial_evaluate(omega, deg_omega, root[i]);
		if(!magnitude)
			continue;
		magnitude = modNN(Log_alpha[magnitude] + root[i]*(J0-1));

		denominator = log_polynomial_evaluate(lambda, deg_lambda, root[i]);
		if(!denominator)
			return -RS_EFORNEY;

		magnitude = Alpha_exp[magnitude + NN - Log_alpha[denominator]];

		if(format.erasure[i] < format.parity)
			parity[format.erasure[i]*format.interleave] ^= magnitude;
		else if(format.erasure[i] < format.n)
			data[(format.erasure[i] - format.parity)*format.interleave] ^= magnitude;
		else
			return -RS_EINVALIDROOT;
	}

	return num_roots;
}
