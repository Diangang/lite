#ifndef LINUX_BITMAP_H
#define LINUX_BITMAP_H

#include <stddef.h>

#define BITS_PER_LONG (sizeof(unsigned long) * 8)
#define BITS_TO_LONGS(nr) (((nr) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define BITMAP_LAST_WORD_MASK(nbits) (~0UL >> (BITS_PER_LONG - ((nbits) % BITS_PER_LONG)))

int __bitmap_empty(const unsigned long *bitmap, int bits);
int __bitmap_full(const unsigned long *bitmap, int bits);
int __bitmap_equal(const unsigned long *bitmap1, const unsigned long *bitmap2, int bits);
void __bitmap_complement(unsigned long *dst, const unsigned long *src, int bits);
void __bitmap_and(unsigned long *dst, const unsigned long *bitmap1, const unsigned long *bitmap2, int bits);
void __bitmap_or(unsigned long *dst, const unsigned long *bitmap1, const unsigned long *bitmap2, int bits);
void __bitmap_xor(unsigned long *dst, const unsigned long *bitmap1, const unsigned long *bitmap2, int bits);

static inline int bitmap_empty(const unsigned long *bitmap, int bits)
{
	return __bitmap_empty(bitmap, bits);
}

static inline int bitmap_full(const unsigned long *bitmap, int bits)
{
	return __bitmap_full(bitmap, bits);
}

static inline int bitmap_equal(const unsigned long *bitmap1, const unsigned long *bitmap2, int bits)
{
	return __bitmap_equal(bitmap1, bitmap2, bits);
}

static inline void bitmap_complement(unsigned long *dst, const unsigned long *src, int bits)
{
	__bitmap_complement(dst, src, bits);
}

static inline void bitmap_and(unsigned long *dst, const unsigned long *bitmap1, const unsigned long *bitmap2, int bits)
{
	__bitmap_and(dst, bitmap1, bitmap2, bits);
}

static inline void bitmap_or(unsigned long *dst, const unsigned long *bitmap1, const unsigned long *bitmap2, int bits)
{
	__bitmap_or(dst, bitmap1, bitmap2, bits);
}

static inline void bitmap_xor(unsigned long *dst, const unsigned long *bitmap1, const unsigned long *bitmap2, int bits)
{
	__bitmap_xor(dst, bitmap1, bitmap2, bits);
}

static inline void bitmap_zero(unsigned long *dst, int bits)
{
	size_t nr = BITS_TO_LONGS(bits);
	for (size_t i = 0; i < nr; i++)
		dst[i] = 0;
}

static inline void bitmap_fill(unsigned long *dst, int bits)
{
	size_t nr = BITS_TO_LONGS(bits);
	for (size_t i = 0; i < nr; i++)
		dst[i] = ~0UL;
	if (bits % BITS_PER_LONG)
		dst[nr - 1] &= BITMAP_LAST_WORD_MASK(bits);
}

static inline void bitmap_copy(unsigned long *dst, const unsigned long *src, int bits)
{
	size_t nr = BITS_TO_LONGS(bits);
	for (size_t i = 0; i < nr; i++)
		dst[i] = src[i];
}

static inline void bitmap_set(unsigned long *dst, int pos, int nbits)
{
	for (int i = 0; i < nbits; i++) {
		int bit = pos + i;
		dst[bit / BITS_PER_LONG] |= 1UL << (bit % BITS_PER_LONG);
	}
}

static inline void bitmap_clear(unsigned long *dst, int pos, int nbits)
{
	for (int i = 0; i < nbits; i++) {
		int bit = pos + i;
		dst[bit / BITS_PER_LONG] &= ~(1UL << (bit % BITS_PER_LONG));
	}
}

static inline int test_bit(int nr, const unsigned long *addr)
{
	return (addr[nr / BITS_PER_LONG] >> (nr % BITS_PER_LONG)) & 1UL;
}

#endif
