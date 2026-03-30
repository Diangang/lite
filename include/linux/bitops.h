#ifndef LINUX_BITOPS_H
#define LINUX_BITOPS_H

#include <stddef.h>

#ifndef BITS_PER_LONG
#define BITS_PER_LONG (sizeof(unsigned long) * 8)
#endif

static inline void set_bit(int nr, unsigned long *addr)
{
	addr[nr / BITS_PER_LONG] |= 1UL << (nr % BITS_PER_LONG);
}

static inline void clear_bit(int nr, unsigned long *addr)
{
	addr[nr / BITS_PER_LONG] &= ~(1UL << (nr % BITS_PER_LONG));
}

static inline int test_bit(int nr, const unsigned long *addr)
{
	return (addr[nr / BITS_PER_LONG] >> (nr % BITS_PER_LONG)) & 1UL;
}

static inline int test_and_set_bit(int nr, unsigned long *addr)
{
	unsigned long mask = 1UL << (nr % BITS_PER_LONG);
	unsigned long *p = &addr[nr / BITS_PER_LONG];
	int old = (*p & mask) != 0;
	*p |= mask;
	return old;
}

static inline int test_and_clear_bit(int nr, unsigned long *addr)
{
	unsigned long mask = 1UL << (nr % BITS_PER_LONG);
	unsigned long *p = &addr[nr / BITS_PER_LONG];
	int old = (*p & mask) != 0;
	*p &= ~mask;
	return old;
}

#endif
