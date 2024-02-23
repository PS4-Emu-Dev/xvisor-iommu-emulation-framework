/*
 * Copyright (c) 2010-2020 Himanshu Chauhan.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * @author: Himanshu Chauhan <hschauhan@nulltrace.org>
 * @brief: MMU related definition and structures.
 */

#ifndef __CPU_MMU_H_
#define __CPU_MMU_H_

#define NR_PML4_PAGES		1
#define NR_PGDP_PAGES		4
#define NR_PGDI_PAGES		8
#define NR_PGTI_PAGES		32
#define NR_IODEV_PAGES		4

#define VMM_CODE_SEG_SEL	0x08
#define VMM_DATA_SEG_SEL	0x10
#define VMM_TSS_SEG_SEL		0x18

/*
 * Bit width and mask for 4 tree levels used in
 * virtual address mapping. 4 tree levels, 9 bit
 * each cover 36-bit of virtual address and reset
 * of the lower 12-bits out of total 48 bits are
 * used as is for page offset.
 *
 *   63-48   47-39  38-30  29-21  20-12   11-0
 * +---------------------------------------------+
 * | UNSED | PML4 | PGDP | PGDI | PGTI | PG OFSET|
 * +---------------------------------------------+
 */
#define PGTREE_BIT_WIDTH	9
#if !defined(__ASSEMBLY__)
#define PGTREE_MASK		~((0x01UL << PGTREE_BIT_WIDTH) - 1)
#else
#define PGTREE_MASK		~((0x01 << PGTREE_BIT_WIDTH) - 1)
#endif

#define PML4_SHIFT		39
#define PML4_MASK		(PGTREE_MASK << PML4_SHIFT)
#define PML4_MAP_MASK		(~((1ULL << PML4_SHIFT) - 1))

#define PGDP_SHIFT		30
#define PGDP_MASK		(PGTREE_MASK << PGDP_SHIFT)
#define PGDP_MAP_MASK		(~((1ULL << PGDP_SHIFT) - 1))

#define PGDI_SHIFT		21
#define PGDI_MASK		(PGTREE_MASK << PGDI_SHIFT)
#define PGDI_MAP_MASK		(~((1ULL << PGDI_SHIFT) - 1))

#define PGTI_SHIFT		12
#define PGTI_MASK		(PGTREE_MASK << PGTI_SHIFT)
#define PGTI_MAP_MASK		(~((1ULL << PGTI_SHIFT) - 1))

#define PAGE_SHIFT		12
#define PAGE_SIZE		(0x01 << PAGE_SHIFT)
#define PAGE_MASK		~(PAGE_SIZE - 1)

#if !defined (__ASSEMBLY__)

#include <vmm_types.h>
#include <vmm_stdio.h>
#include <vmm_spinlocks.h>
#include <libs/list.h>

extern struct pgtbl_ctrl host_pgtbl_ctl;

#define PAGE_SIZE_1G	(0x40000000UL)
#define PAGE_SIZE_2M	(0x200000UL)
#define PAGE_SIZE_4K	(0x1000UL)

static inline void invalidate_vaddr_tlb(virtual_addr_t vaddr)
{
	__asm__ __volatile__("invlpg (%0)\n\t"
			     ::"r"(vaddr):"memory");
}

union page32 {
	u32 _val;
	struct {
		u32 present:1;
		u32 rw:1;
		u32 priviledge:1;
		u32 write_through:1;
		u32 cache_disable:1;
		u32 accessed:1;
		u32 dirty:1;
		u32 pat:1;
		u32 global:1;
		u32 avl:3;
		u32 paddr:20;
	};
};

union page {
	u64 _val;
	struct {
		u64 present:1;
		u64 rw:1;
		u64 priviledge:1;
		u64 write_through:1;
		u64 cache_disable:1;
		u64 accessed:1;
		u64 dirty:1;
		u64 pat:1;
		u64 global:1;
		u64 ignored:3;
		u64 paddr:40;
		u64 reserved:11;
		u64 execution_disable:1;
	} bits;
};

#define PGPROT_MASK	(~PAGE_MASK)

/* Page Helpers */
static inline bool PageReadOnly(union page32 *pg)
{
	return (!pg->rw);
}

static inline bool PagePresent(union page32 *pg)
{
	return (pg->present);
}

static inline bool PageGlobal(union page32 *pg)
{
	return (pg->global);
}

static inline bool PageCacheable(union page32 *pg)
{
	return (!pg->cache_disable);
}

static inline bool PageDirty(union page32 *pg)
{
	return (pg->dirty);
}

static inline bool PageAccessed(union page32 *pg)
{
	return (pg->accessed);
}

static inline bool PageWriteThrough(union page32 *pg)
{
	return (pg->write_through);
}

static inline void SetPageReadOnly(union page32 *pg)
{
	pg->rw = 0;
}

static inline void SetPageReadWrite(union page32 *pg)
{
	pg->rw = 1;
}

static inline void SetPagePresent(union page32 *pg)
{
	pg->present = 1;
}

static inline void SetPageAbsent(union page32 *pg)
{
	pg->present = 0;
}

static inline void SetPageGlobal(union page32 *pg)
{
	pg->global = 1;
}

static inline void SetPageLocal(union page32 *pg)
{
	pg->global = 0;
}

static inline void SetPageCacheable(union page32 *pg)
{
	pg->cache_disable = 0;
}

static inline void SetPageUncacheable(union page32 *pg)
{
	pg->cache_disable = 1;
}

static inline void SetPageDirty(union page32 *pg)
{
	pg->dirty = 1;
}

static inline void SetPageWashed(union page32 *pg)
{
	pg->dirty = 0;
}

static inline void SetPageAccessed(union page32 *pg)
{
	pg->accessed = 1;
}

static inline void SetPageUnaccessed(union page32 *pg)
{
	pg->accessed = 0;
}

static inline void SetPageWriteThrough(union page32 *pg)
{
	pg->write_through = 1;
}

static inline void SetPageNoWriteThrough(union page32 *pg)
{
	pg->write_through = 0;
}

static inline void SetPageProt(union page32 *pg, u32 pgprot)
{
	pg->_val &= ~PGPROT_MASK;
	pg->_val |= pgprot;
}

struct page_table {
	struct dlist head;
	struct page_table *parent;
	int level;
	int stage;
	physical_addr_t map_ia;
	physical_addr_t tbl_pa;
	vmm_spinlock_t tbl_lock; /*< Lock to protect table contents, 
				      entry_cnt, child_cnt, and child_list 
				  */
	virtual_addr_t tbl_va;
	u32 pte_cnt;
	u32 child_cnt;
	struct dlist child_list;
};

union seg_attrs {
	u16 bytes;
	struct {
		u16 type:4;
		u16 s:1;
		u16 dpl:2;
		u16 p:1;
		u16 avl:1;
		u16 l:1;
		u16 db:1;
		u16 g:1;
	} fields;
} __packed;

struct seg_selector {
	u16		sel;
	union seg_attrs	attrs;
	u32		limit;
	u64		base;
} __packed;

static inline void dump_seg_selector(const char *seg_name, struct seg_selector *ss)
{
	vmm_printf("%-6s: Sel: 0x%08x Limit: 0x%08x Base: 0x%08lx (G: %2u DB: %2u L: %2u AVL: %2u P: %2u DPL: %2u S: %2u Type: %2d)\n",
		   seg_name, ss->sel, ss->limit, ss->base, ss->attrs.fields.g, ss->attrs.fields.db, ss->attrs.fields.l,
		   ss->attrs.fields.avl, ss->attrs.fields.p, ss->attrs.fields.dpl, ss->attrs.fields.s, ss->attrs.fields.type);
}

#endif

#endif /* __CPU_MMU_H_ */
