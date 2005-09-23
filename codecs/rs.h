/*
 * Global definitions for Reed-Solomon encoder/decoder
 *
 * Copyright (C) 2000,2001,2002 Kipp C. Cannon
 * Portions Copyright (C) 1999 Phil Karn, KA9Q
 */

#ifndef RS_H
#define RS_H

/*
 * Decoder error conditions.
 */

#define RS_EDEGENERATEROOTS  1  /* lambda(x) has degenerate roots */
#define RS_EFORNEY           2  /* divide by 0 in Forney algorithm */
#define RS_EINVALIDROOT      3  /* erasure location beyond code length */

/*
 * Set MM to be the size of each code symbol in bits. The maximum
 * Reed-Solomon block size will then be 2^MM - 1 symbols.
 */

#define MM  8


/*
 * Define the type used for storing code word symbols.
 */

#if MM <= 8
typedef unsigned char rs_symbol_t;
#else
typedef unsigned short rs_symbol_t;
#endif


/*
 * Define the type used for storing elements of the Galois field used by
 * the code.  Technically, it should be the same as rs_symbol_t but since
 * it is only used privately within the encoder/decoder it need not be.
 * Indeed, on many platforms performance can be improved if a different
 * choice is made (usually int).
 */

typedef int gf;


/*
 * Reed-Solomon encoder/decoder format descriptor.
 */

typedef struct {
	int  n;                 /* number of symbols in code word */
	int  k;                 /* number of data symbols in code word */
	int  parity;            /* number of parity symbols in code word */
	int  interleave;        /* distance b/w symbols (contiguous = 1) */
	int  remainder_start;   /* initializer for encoder's remainder index */
	gf  *g;                 /* generator polynomial g(x) in alpha rep */
	gf  *erasure;           /* error locations */
	gf  *log_beta;          /* return the power of beta equal to alpha^i */
} rs_format_t;


/*
 * galois_field_init()
 *
 * Call this first.
 *
 * Constructs look-up tables for performing arithmetic over the Galois
 * field GF(2^MM) from the irreducible polynomial p(x) stored in p.
 *
 * This function constructs the look-up tables called Log_alpha[] and
 * Alpha_exp[] from p.  Since 0 cannot be represented as a power of alpha,
 * special elements are added to the look-up tables to represent this case
 * and all multiplications must first check for this.  We call this special
 * element INFINITY since it is the result of taking the log (base alpha)
 * of zero.  Also, we duplicate Alpha_exp[] to reduce our reliance on
 * modNN().
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
 * sizes from 2 to 16 bits can be found below.
 */

void galois_field_init(int p);


/*
 * Generator polynomials for galois_field_init() expressed in base 8.  For
 * example, GF00256 is the generator polynomial for GF(2^8).  Many of these
 * are not unique.  You can supply your own if you wish.
 */


#define GF00004	0000007	/* x^2 + x + 1               */
#define GF00008	0000013	/* x^3 + x + 1               */
#define GF00016	0000023	/* x^4 + x + 1               */
#define GF00032	0000045	/* x^5 + x^2 + 1             */
#define GF00064	0000103	/* x^6 + x + 1               */
#define GF00128	0000211	/* x^7 + x^3 + 1             */
#define GF00256	0000435	/* x^8 + x^4 + x^3 + x^2 + 1 */
#define GF00512	0001021	/* x^9 + x^4 + 1             */
#define GF01024	0002011	/* x^10 + x^3 + 1            */
#define GF02048	0004005	/* x^11 + x^2 + 1            */
#define GF04096	0010123	/* x^12 + x^6 + x^4 + x + 1  */
#define GF08192	0020033	/* x^13 + x^4 + x^3 + x + 1  */
#define GF16384	0042103	/* x^14 + x^10 + x^6 + x + 1 */
#define GF32768	0100003	/* x^15 + x + 1              */
#define GF65536	0210013	/* x^16 + x^12 + x^3 + x + 1 */


/*
 * Reed-Solomon encoder/decoder setup routine.
 *
 * Must be called before the encoder or decoder are called.  The code
 * parameters are used to initialize a format structure.  Checking is done
 * on the parameters and the return value is < 0 if they are invalid and
 * the format structure is unchanged otherwise 0 is returned.
 *
 * Parameters:
 *   n
 *      Specifies the size of the code vector in symbols.  This must not
 *      exceed 2^MM - 1 (eg. 255 for 8-bit symbols).
 *   k
 *      Specifies the number of data symbols in the code vector.  The
 *      number of parity symbols generated is (n-k) and the code will be
 *      able to correct up to (n-k) erasures or (n-k)/2 errors or any
 *      combination thereof with each error counting as two erasures.
 *   interleave
 *      Specifies the distance, in symbols, from one symbol to the next in
 *      the data and parity buffers.  Normally this is set to 1 for symbols
 *      that are packed contiguously into the buffers but can be set to a
 *      larger number to allow multiple codewords to be ``interleaved''
 *      into one another.  This is illustrated in the following diagram
 *      which shows an array of symbols and how an interleave setting of
 *      either 1 or 2 affects the use of those symbols.  In the case of an
 *      interleave of 2, it is possible to interleave the symbols of a
 *      second code vector between the symbols of the first.
 *
 *      +---+---+---+---+---+---+---+-
 *      |   |   |   |   |   |   |   | . . .    Array of symbols
 *      +---+---+---+---+---+---+---+-
 *
 *        0   1   2   3   4   5   6   . . .    Interleave = 1
 *
 *        0       1       2       3   . . .    Interleave = 2
 *            0       1       2
 *
 *   format
 *      Pointer to the format structure to initialize.
 */

int reed_solomon_codec_new(unsigned int n, unsigned int k, int interleave, rs_format_t *format);


/*
 * Reed-Solomon encoder/decoder clean-up.
 *
 * Call this when finished to free all allocated memory.
 */

void reed_solomon_codec_free(rs_format_t *format);


/*
 * Reed-Solomon encoder
 *
 * The data symbols to be encoded are passed to the encoder in data[].  The
 * computed parity symbols are placed in parity[] which must be large
 * enough to contain them.
 */

void reed_solomon_encode(rs_symbol_t *parity, const rs_symbol_t *data, rs_format_t format);


/*
 * Reed-Solomon erasures-and-errors decoding
 *
 * The received vector is split into data symbols which are passed in
 * data[] and parity symbols which are passed in parity[].  A list of
 * zero-origin erasure positions, if any, can be passed in
 * format->erasure[] with the count of the erasures in num_erase.  Pass 0
 * for num_erase if no erasures are known.
 *
 * The decoder corrects the symbols (both data and parity) in place (if
 * possible) and returns the number of symbols corrected.  format->erasure
 * is left filled with the computed error and erasure positions.  If the
 * codeword is illegal or uncorrectable, the block[] array is unchanged and
 * -1 is returned.
 */

int reed_solomon_decode(rs_symbol_t *parity, rs_symbol_t *data, int num_erase, rs_format_t format);

#endif /* RS_H */
