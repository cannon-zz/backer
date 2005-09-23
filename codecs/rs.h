/*
 * Global definitions for Reed-Solomon encoder/decoder
 *
 * Copyright (C) 2000,2001 Kipp C. Cannon
 * Portions Copyright (C) 1999 Phil Karn, KA9Q
 */

#ifndef RS_H
#define RS_H

/*
 * Set MM to be the size of each code symbol in bits. The Reed-Solomon
 * block size will then be NN = 2^M - 1 symbols.
 */

#define MM  8

/*
 * Set MAX_PARITY to be the maximum allowed number of parity symbols in
 * each block.  The actual number is determined by the parameters passed to
 * the init function but some arrays need to be pre-allocated by the code
 * and this is used to set them to some reasonable size.  Dynamically
 * allocating the arrays would slow down the encoder/decoder and most
 * applications can specify a limit so this shouldn't be a problem.
 */

#define MAX_PARITY  32

/*
 * Set LOG_BETA to the power of alpha used to generate the roots of the
 * generator polynomial and set J0 to the power of beta to be used as the
 * first root.  The generator polynomial will then have roots
 *
 *    beta^J0, beta^(J0+1), beta^(J0+2), ..., beta^(J0+2t-1)
 *
 * where beta = alpha^LOG_BETA and 2t is the number of parity symbols in
 * the code.  A conventional Reed-Solomon encoder is selected by setting J0
 * = 1, LOG_BETA = 1.
 */

#define J0        1
#define LOG_BETA  1

/*
 * NN is the number of non-zero symbols in the Galois Field.  This is also
 * the maximum number of symbols which can be used to form a code word.
 * Actually all code words have exactly this many symbols but shortened
 * codes can be formed with fewer than this by padding them with zeros for
 * encoding and decoding.  This parameter is determined by the symbol size
 * so don't edit it.
 */

#define	NN  ((1 << MM) - 1)

/*
 * Set the data type for storing elements of the code word
 */

#if MM <= 8
typedef unsigned char data_t;
#else
typedef unsigned short data_t;
#endif

/*
 * This defines the type used to store an element of the Galois Field
 * used by the code.
 */

typedef int gf;


/*
 * Reed-Solomon encoder/decoder format descriptor.
 */

typedef struct
	{
	int  n;                 /* code word size in symbols */
	int  k;                 /* data symbols used in code */
	int  parity;            /* parity symbols used in code */
	int  remainder_start;   /* initializer for encoder's remainder index */
	gf  g[MAX_PARITY+1];    /* generator polynomial g(x) in alpha rep */
	} rs_format_t;

/*
 * Reed-Solomon encoder/decoder initialization routine.
 *
 * Must be called before the encoder or decoder are called.  The code
 * parameters are used to initialize a format structure.  Checking is done
 * on the parameters and the return value is < 0 if they are invalid and
 * the format structure is unchanged otherwise 0 is returned.
 *
 * Parameters for rs_init():
 *   n
 *      Specifies the size of the code vector.  This must not exceed
 *      2^MM - 1 (eg. 255 for 8-bit symbols).
 *   k
 *      Specifies the number of data symbols in the block.  The number of
 *      parity symbols generated is (n-k) and the code will be able to
 *      correct up to (n-k) erasures or (n-k)/2 errors or any combination
 *      thereof with each error counting as two erasures.
 *   rs_format
 *      Pointer to the format structure to initialize.
 */

int reed_solomon_init(unsigned int n, unsigned int k, rs_format_t *rs_format);

/*
 * Reed-Solomon encoder
 *
 * The data symbols to be encoded are passed to the encoder in data[].  The
 * computed parity symbols are placed in parity[] which must be large
 * enough to contain them.
 */

void reed_solomon_encode(data_t *parity, data_t *data, rs_format_t *rs_format);

/*
 * Reed-Solomon erasures-and-errors decoding
 *
 * The received vector is split into data symbols which are passed in
 * data[] and parity symbols which are passed in parity[].  A list of
 * zero-origin erasure positions, if any, goes in erasure[] with the count
 * of erasures in no_eras.  Pass NULL for eras_pos and 0 for no_eras if no
 * erasures are known.  The block format is passed as a pointer to an
 * rs_format_t structure.
 *
 * The decoder corrects the symbols (both data and parity) in place (if
 * possible) and returns the number of corrected symbols. If the codeword
 * is illegal or uncorrectible, the block[] array is unchanged and -1 is
 * returned
 *
 * If erasure[] is non-NULL, it will be filled with the computed error and
 * erasure positions.  It must be large enough to hold n-k elements.
 */

int reed_solomon_decode(data_t *parity, data_t *data, gf *erasure, int num_erase, rs_format_t *rs_format);


/*
 * Parameter checks
 */

#if (MM < 2) || (MM > 16)
#error "MM must be set in range 2 <= MM <= 16"
#endif  /* MM */

#endif /* RS_H */
