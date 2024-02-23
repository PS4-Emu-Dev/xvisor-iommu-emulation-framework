/*
 * Copyright (c) 2014 Himanshu Chauhan.
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
 * @brief: Architecture specific guest handling.
 */

#ifndef __ARCH_GUEST_HELPER_H_
#define __ARCH_GUEST_HELPER_H_

#include <cpu_vm.h>
#include <emu/rtc/mc146818rtc.h>
#include <emu/i8259.h>
#include <vmm_manager.h>
#include <vm/svm_intercept.h>

#define EPT_BOOT_PAGE	(0xFFFFF000UL)

#define IS_BIOS_ADDRESS(_address_flag)			\
	(_address_flag & (VMM_REGION_READONLY		\
			  | VMM_REGION_ISROM		\
			  | VMM_REGION_REAL))

#define GUEST_HALT_SW_CODE	0x80
/* When CPU exited from VM mode for VMM to handle */
#define GUEST_VM_EXIT_SW_CODE	0x81

/*! \brief x86 Guest private information
 *
 * This contains the private information for x86
 * specific guest.
 */
struct x86_guest_priv {
	/**< List of all PICs associated with guest. Guest code is not directly
	 * know about any of the fields. PIC emulator will set this and query
	 * when required.
	 */
	void *pic_list;
	struct cmos_rtc_state *rtc_cmos;
	struct i8259_state *master_pic;
	u64 tot_ram_sz;
};

/*!def x86_guest_priv(guest) is to access guest private information */
#define x86_guest_priv(guest) ((struct x86_guest_priv *)(guest->arch_priv))

extern int gva_to_gpa(struct vcpu_hw_context *context, virtual_addr_t vaddr,
		      physical_addr_t *gpa);
extern int gpa_to_hpa(struct vcpu_hw_context *context, physical_addr_t vaddr,
		      physical_addr_t *hpa);
extern virtual_addr_t get_free_page_for_pagemap(struct vcpu_hw_context *context,
						physical_addr_t *page_phys);
extern int purge_guest_shadow_pagetable(struct vcpu_hw_context *context);
extern int create_guest_shadow_map(struct vcpu_hw_context *context,
				   virtual_addr_t vaddr, physical_addr_t paddr,
				   size_t size, u32 pdprot, u32 pgprot);
extern int update_guest_shadow_pgprot(struct vcpu_hw_context *context,
				      virtual_addr_t vaddr,
				      u32 level,
				      u32 pgprot);
extern int purge_guest_shadow_map(struct vcpu_hw_context *context,
				  virtual_addr_t vaddr,
				  size_t size);
extern int lookup_guest_pagetable(struct vcpu_hw_context *context,
				  physical_addr_t fault_addr,
				  physical_addr_t *lookedup_addr,
				  union page32 *pde,
				  union page32 *pte);
extern int lookup_shadow_pagetable(struct vcpu_hw_context *context,
				   physical_addr_t fault_addr,
				   physical_addr_t *lookedup_addr,
				   union page32 *pde,
				   union page32 *pte);
extern void invalidate_shadow_entry(struct vcpu_hw_context *context,
				    virtual_addr_t invl_va);
extern void mark_guest_interrupt_pending(struct vcpu_hw_context *context,
					 u32 intno);
extern void inject_guest_exception(struct vcpu_hw_context *context,
				   u32 exception);
extern void inject_guest_interrupt(struct vcpu_hw_context *context,
				   u32 intno);
extern void invalidate_guest_tlb(struct vcpu_hw_context *context, u32 inval_va);
extern void arch_guest_halt(struct vmm_guest *guest);
extern void arch_guest_handle_vm_exit(struct vcpu_hw_context *context);

#endif /* __ARCH_GUEST_HELPER_H_ */
