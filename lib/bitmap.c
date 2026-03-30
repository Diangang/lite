#include "../include/linux/bitmap.h"

int __bitmap_empty(const unsigned long *bitmap, int bits)
{
	int lim = bits / BITS_PER_LONG;
	for (int k = 0; k < lim; ++k)
		if (bitmap[k])
			return 0;

	if (bits % BITS_PER_LONG)
		if (bitmap[lim] & BITMAP_LAST_WORD_MASK(bits))
			return 0;

	return 1;
}

int __bitmap_full(const unsigned long *bitmap, int bits)
{
	int lim = bits / BITS_PER_LONG;
	for (int k = 0; k < lim; ++k)
		if (~bitmap[k])
			return 0;

	if (bits % BITS_PER_LONG)
		if (~bitmap[lim] & BITMAP_LAST_WORD_MASK(bits))
			return 0;

	return 1;
}

int __bitmap_equal(const unsigned long *bitmap1, const unsigned long *bitmap2, int bits)
{
	int lim = bits / BITS_PER_LONG;
	for (int k = 0; k < lim; ++k)
		if (bitmap1[k] != bitmap2[k])
			return 0;

	if (bits % BITS_PER_LONG)
		if ((bitmap1[lim] ^ bitmap2[lim]) & BITMAP_LAST_WORD_MASK(bits))
			return 0;

	return 1;
}

void __bitmap_complement(unsigned long *dst, const unsigned long *src, int bits)
{
	int lim = bits / BITS_PER_LONG;
	for (int k = 0; k < lim; ++k)
		dst[k] = ~src[k];

	if (bits % BITS_PER_LONG)
		dst[lim] = ~src[lim] & BITMAP_LAST_WORD_MASK(bits);
}

void __bitmap_and(unsigned long *dst, const unsigned long *bitmap1, const unsigned long *bitmap2, int bits)
{
	int nr = BITS_TO_LONGS(bits);
	for (int k = 0; k < nr; k++)
		dst[k] = bitmap1[k] & bitmap2[k];
}

void __bitmap_or(unsigned long *dst, const unsigned long *bitmap1, const unsigned long *bitmap2, int bits)
{
	int nr = BITS_TO_LONGS(bits);
	for (int k = 0; k < nr; k++)
		dst[k] = bitmap1[k] | bitmap2[k];
}

void __bitmap_xor(unsigned long *dst, const unsigned long *bitmap1, const unsigned long *bitmap2, int bits)
{
	int nr = BITS_TO_LONGS(bits);
	for (int k = 0; k < nr; k++)
		dst[k] = bitmap1[k] ^ bitmap2[k];
}
