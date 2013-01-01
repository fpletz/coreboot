/*
 * (C) Copyright 2002
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

/* FIXME(dhendrix): clean-up weak symbols if it looks unlikely we'll
   want to override them with anything other than what's in cache_v7. */
#include <common.h>
#include <stdlib.h>
#include <system.h>
#include <global_data.h>

#if !(defined(CONFIG_SYS_ICACHE_OFF) && defined(CONFIG_SYS_DCACHE_OFF))

DECLARE_GLOBAL_DATA_PTR;

#if 0
void __arm_init_before_mmu(void)
{
}
void arm_init_before_mmu(void)
	__attribute__((weak, alias("__arm_init_before_mmu")));
#endif

static void cp_delay (void)
{
	volatile int i;

	/* copro seems to need some delay between reading and writing */
	for (i = 0; i < 100; i++)
		nop();
	asm volatile("" : : : "memory");
}

static void set_section_dcache(int section, enum dcache_option option)
{
	u32 value = section << MMU_SECTION_SHIFT | (3 << 10);
//	u32 *page_table = (u32 *)gd->tlb_addr;
	u32 *page_table;
	unsigned int tlb_addr;
	unsigned int tlb_size = 4096 * 4;

	/*
	 * FIXME(dhendrix): This calculation is from arch/arm/lib/board.c
	 * in u-boot. We may need to subtract more due to logging.
	 */
	tlb_addr = (CONFIG_SYS_SDRAM_BASE + (CONFIG_DRAM_SIZE_MB << 20UL));
	tlb_addr -= tlb_size;
	/* round down to next 64KB limit */
	tlb_addr &= ~(0x10000 - 1);
	page_table = (u32 *)tlb_addr;

	switch (option) {
	case DCACHE_WRITETHROUGH:
		value |= 0x1a;
		break;

	case DCACHE_WRITEBACK:
		value |= 0x1e;
		break;

	case DCACHE_OFF:
		value |= 0x12;
		break;
	}

	page_table[section] = value;
}

#if 0
void __mmu_page_table_flush(unsigned long start, unsigned long stop)
{
	debug("%s: Warning: not implemented\n", __func__);
}
#endif

#if 0
void mmu_page_table_flush(unsigned long start, unsigned long stop)
	__attribute__((weak, alias("__mmu_page_table_flush")));
#endif

void mmu_set_region_dcache(unsigned long start, int size, enum dcache_option option)
{
	u32 *page_table = (u32 *)gd->tlb_addr;
	u32 upto, end;

	end = ALIGN(start + size, MMU_SECTION_SIZE) >> MMU_SECTION_SHIFT;
	start = start >> MMU_SECTION_SHIFT;
	debug("mmu_set_region_dcache start=%x, size=%x, option=%d\n",
	      start, size, option);
	for (upto = start; upto < end; upto++)
		set_section_dcache(upto, option);
	mmu_page_table_flush((u32)&page_table[start], (u32)&page_table[end]);
}

#if 0
static inline void dram_bank_mmu_setup(int bank)
{
//	bd_t *bd = gd->bd;
	int	i;

	debug("%s: bank: %d\n", __func__, bank);
	for (i = bd->bi_dram[bank].start >> 20;
	     i < (bd->bi_dram[bank].start + bd->bi_dram[bank].size) >> 20;
	     i++) {
#if defined(CONFIG_SYS_ARM_CACHE_WRITETHROUGH)
		set_section_dcache(i, DCACHE_WRITETHROUGH);
#else
		set_section_dcache(i, DCACHE_WRITEBACK);
#endif
	}
}
#endif

/* FIXME(dhendrix): modified to take arguments from the caller (mainboard's
   romstage.c) so it doesn't rely on global data struct */
/**
 * dram_bank_mmu_set - set up the data cache policy for a given dram bank
 *
 * @start:	virtual address start of bank
 * @size:	size of bank (in bytes)
 */
inline void dram_bank_mmu_setup(unsigned long start, unsigned long size)
{
	int	i;

	debug("%s: bank: %d\n", __func__, bank);
	for (i = start >> 20; i < (start + size) >> 20; i++) {
#if defined(CONFIG_ARM_DCACHE_POLICY_WRITEBACK)
		set_section_dcache(i, DCACHE_WRITEBACK);
#elif defined(CONFIG_ARM_DCACHE_POLICY_WRITETHROUGH)
		set_section_dcache(i, DCACHE_WRITETHROUGH);
#else
#error "Must define dcache policy."
#endif
	}
}

/* to activate the MMU we need to set up virtual memory: use 1M areas */
static inline void mmu_setup(void)
{
	int i;
	u32 reg;

	arm_init_before_mmu();
	/* Set up an identity-mapping for all 4GB, rw for everyone */
	for (i = 0; i < 4096; i++)
		set_section_dcache(i, DCACHE_OFF);

	/* FIXME(dhendrix): u-boot's global data struct was used here... */
#if 0
	for (i = 0; i < CONFIG_NR_DRAM_BANKS; i++) {
		dram_bank_mmu_setup(i);
	}
#endif
#if 0
	/* comes from board's romstage.c, since we need to know which
	   ranges to setup */
	mainboard_setup_mmu();
#endif
	dram_bank_mmu_setup(CONFIG_SYS_SDRAM_BASE, CONFIG_DRAM_SIZE_MB << 20);

	/* Copy the page table address to cp15 */
	asm volatile("mcr p15, 0, %0, c2, c0, 0"
		     : : "r" (gd->tlb_addr) : "memory");
	/* Set the access control to all-supervisor */
	asm volatile("mcr p15, 0, %0, c3, c0, 0"
		     : : "r" (~0));
	/* and enable the mmu */
	reg = get_cr();	/* get control reg. */
	cp_delay();
	set_cr(reg | CR_M);
}

static int mmu_enabled(void)
{
	return get_cr() & CR_M;
}

/* cache_bit must be either CR_I or CR_C */
static void cache_enable(uint32_t cache_bit)
{
	uint32_t reg;

	/* The data cache is not active unless the mmu is enabled too */
	if ((cache_bit == CR_C) && !mmu_enabled())
		mmu_setup();
	reg = get_cr();	/* get control reg. */
	cp_delay();
	set_cr(reg | cache_bit);
}

/*
 * Big hack warning!
 *
 * Devs like to compile with -O0 to get a nice debugging illusion. But this
 * function does not survive that since -O0 causes the compiler to read the
 * PC back from the stack after the dcache flush. Might it be possible to fix
 * this by flushing the write buffer?
 */
static void cache_disable(uint32_t cache_bit) __attribute__ ((optimize(2)));

/* cache_bit must be either CR_I or CR_C */
static void cache_disable(uint32_t cache_bit)
{
	uint32_t reg;

	if (cache_bit == CR_C) {
		/* if cache isn;t enabled no need to disable */
		reg = get_cr();
		if ((reg & CR_C) != CR_C)
			return;
		/* if disabling data cache, disable mmu too */
		cache_bit |= CR_M;
	}
	reg = get_cr();
	cp_delay();
	if (cache_bit == (CR_C | CR_M))
		flush_dcache_all();
	set_cr(reg & ~cache_bit);
}
#endif

#ifdef CONFIG_SYS_ICACHE_OFF
void icache_enable (void)
{
	return;
}

void icache_disable (void)
{
	return;
}

int icache_status (void)
{
	return 0;					/* always off */
}
#else
void icache_enable(void)
{
	cache_enable(CR_I);
}

void icache_disable(void)
{
	cache_disable(CR_I);
}

int icache_status(void)
{
	return (get_cr() & CR_I) != 0;
}
#endif

#ifdef CONFIG_SYS_DCACHE_OFF
void dcache_enable (void)
{
	return;
}

void dcache_disable (void)
{
	return;
}

int dcache_status (void)
{
	return 0;					/* always off */
}
#else
void dcache_enable(void)
{
	cache_enable(CR_C);
}

void dcache_disable(void)
{
	cache_disable(CR_C);
}

int dcache_status(void)
{
	return (get_cr() & CR_C) != 0;
}
#endif