/*
 * This file is part of the coreboot project.
 *
 * Copyright 2013 Google Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * cache.c: Low-level cache operations for ARMv7
 */

#include <types.h>

#include <arch/cache.h>

#define bitmask(high, low) ((1UL << (high)) + \
			((1UL << (high)) - 1) - ((1UL << (low)) - 1))

/* Basic log2() implementation. Note: log2(0) is 0 for our purposes. */
/* FIXME: src/include/lib.h is difficult to work with due to romcc */
static unsigned long log2(unsigned long u)
{
	int i = 0;

	while (u >>= 1)
		i++;

	return i;
}

void tlb_invalidate_all(void)
{
	/*
	 * FIXME: ARMv7 Architecture Ref. Manual claims that the distinction
	 * instruction vs. data TLBs is deprecated in ARMv7. But that doesn't
	 * really seem true for Cortex-A15?
	 */
	tlbiall();
	dtlbiall();
	itlbiall();
	isb();
	dsb();
}

void icache_invalidate_all(void)
{
	/* icache can be entirely invalidated with one operation.
	 * Note: If branch predictors are architecturally-visible, ICIALLU
	 * also performs a BPIALL operation (B2-1283 in arch manual)
	 */
	iciallu();
	isb();
}

enum dcache_op {
	OP_DCCISW,
	OP_DCISW
};

/* do a dcache operation on entire cache by set/way */
static void dcache_op_set_way(enum dcache_op op)
{
	uint32_t ccsidr;
	unsigned int associativity, num_sets, linesize_bytes;
	unsigned int set, way;
	unsigned int level;

	level = (read_csselr() >> 1) & 0x7;

	/*
	 * dcache must be invalidated by set/way for portability since virtual
	 * memory mapping is system-defined. The number of sets and
	 * associativity is given by CCSIDR. We'll use DCISW to invalidate the
	 * dcache.
	 */
	ccsidr = read_ccsidr();

	/* FIXME: rounding up required here? */
	num_sets = ((ccsidr & bitmask(27, 13)) >> 13) + 1;
	associativity = ((ccsidr & bitmask(12, 3)) >> 3) + 1;
	/* FIXME: do we need to use CTR.DminLine here? */
	linesize_bytes = (1 << ((ccsidr & 0x7) + 2)) * 4;

	/*
	 * Set/way operations require an interesting bit packing. See section
	 * B4-35 in the ARMv7 Architecture Reference Manual:
	 *
	 * A: Log2(associativity)
	 * B: L+S
	 * L: Log2(linesize)
	 * S: Log2(num_sets)
	 *
	 * The bits are packed as follows:
	 *  31  31-A        B B-1    L L-1   4 3   1 0
	 * |---|-------------|--------|-------|-----|-|
	 * |Way|    zeros    |   Set  | zeros |level|0|
	 * |---|-------------|--------|-------|-----|-|
	 */
	for (way = 0; way < associativity; way++) {
		for (set = 0; set < num_sets; set++) {
			uint32_t val = 0;
			val |= way << (32 - log2(associativity));
			val |= set << log2(linesize_bytes);
			val |= level << 1;
			switch(op) {
			case OP_DCCISW:
				dccisw(val);
				break;
			case OP_DCISW:
				dcisw(val);
				break;
			default:
				break;
			}
		}
	}

	dsb();
}

void dcache_clean_invalidate_all(void)
{
	dcache_op_set_way(OP_DCCISW);
}

void dcache_invalidate_all(void)
{
	dcache_op_set_way(OP_DCISW);
}

static unsigned int line_bytes(void)
{
	uint32_t ccsidr;
	unsigned int size;

	ccsidr = read_ccsidr();
	/* [2:0] - Indicates (Log2(number of words in cache line)) - 2 */
	size = 1 << ((ccsidr & 0x7) + 2);	/* words per line */
	size *= sizeof(unsigned int);		/* bytes per line */

	return size;
}

void dcache_clean_invalidate_by_mva(unsigned long addr, unsigned long len)
{
	unsigned long line, i;

	line = line_bytes();
	for (i = addr & ~(line - 1); i < addr + len - 1; i += line)
		dccimvac(addr);
}

/* FIXME: wrapper around imported mmu_setup() for now */
extern void mmu_setup(unsigned long start, unsigned long size);
void mmu_setup_by_mva(unsigned long start, unsigned long size)
{
	mmu_setup(start, size);
}