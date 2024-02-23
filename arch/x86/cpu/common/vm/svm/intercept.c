/**
 * Copyright (c) 2013 Himanshu Chauhan.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * @file amd_intercept.c
 * @author Himanshu Chauhan (hschauhan@nulltrace.org)
 * @brief SVM intercept handling/registration code.
 */
#include <vmm_error.h>
#include <vmm_types.h>
#include <vmm_stdio.h>
#include <vmm_host_aspace.h>
#include <vmm_guest_aspace.h>
#include <cpu_vm.h>
#include <cpu_inst_decode.h>
#include <cpu_features.h>
#include <cpu_mmu.h>
#include <cpu_pgtbl_helper.h>
#include <arch_guest_helper.h>
#include <vm/svm_intercept.h>
#include <vm/svm.h>
#include <vmm_devemu.h>
#include <vmm_manager.h>
#include <vmm_main.h>
#include <x86_debug_log.h>

DEFINE_X86_DEBUG_LOG_SUBSYS_LEVEL(svm_intercept, X86_DEBUG_LOG_LVL_INFO);

static char *exception_names[] = {
	"#DivError",	/* 0 */
	"#Debug",	/* 1 */
	"#NMI",		/* 2 */
	"#Breakpoint",	/* 3 */
	"#Overflow",	/* 4 */
	"#OutOfBounds",	/* 5 */
	"#InvOpcode",	/* 6 */
	"#NoDev",	/* 7 */
	"#DoubleFault",	/* 8 */
	"#CoprocOvrrun",/* 9 */
	"#InvalTSS",	/* 10 */
	"#MissingSeg",	/* 11 */
	"#MissingStack",/* 12 */
	"#GPF",		/* 13 */
	"#PGFault",	/* 14 */
	"#CoprocErr",	/* 15 */
	"#AlignCheck",	/* 16 */
	"#MachineCheck",/* 17 */
	"#SIMDErr",	/* 18 */
	"#Unknown19",	/* 19 */
	"#Unknown20",	/* 20 */
	"#Unknown21",	/* 21 */
	"#Unknown22",	/* 22 */
	"#Unknown23",	/* 23 */
	"#Unknown24",	/* 24 */
	"#Unknown25",	/* 25 */
	"#Unknown26",	/* 26 */
	"#Unknown27",	/* 27 */
	"#Unknown28",	/* 28 */
	"#Unknown29",	/* 29 */
	"#Unknown30",	/* 30 */
	"#Unknown31",	/* 31 */
};

static inline int guest_in_realmode(struct vcpu_hw_context *context)
{
	return (!(context->vmcb->cr0 & X86_CR0_PE));
}

static int guest_read_gva(struct vcpu_hw_context *context, u32 vaddr, u8 *where
			  , u32 size)
{
	physical_addr_t gphys;

	if (gva_to_gpa(context, vaddr, &gphys)) {
		X86_DEBUG_LOG(svm_intercept, LVL_ERR, "Failed to convert guest virtual 0x%x to guest "
		       "physical.\n", vaddr);
		return VMM_EFAIL;
	}

	/* FIXME: Should we always do cacheable memory access here ?? */
	if (vmm_guest_memory_read(context->assoc_vcpu->guest, gphys,
				  where, size, TRUE) < size) {
		X86_DEBUG_LOG(svm_intercept, LVL_ERR, "Failed to read guest pa 0x%lx\n", gphys);
		return VMM_EFAIL;
	}

	return VMM_OK;
}

static int guest_read_fault_inst(struct vcpu_hw_context *context,
				 x86_inst *g_ins)
{
	physical_addr_t rip_phys;
	struct vmm_guest *guest = context->assoc_vcpu->guest;

	if (gva_to_gpa(context, context->vmcb->rip, &rip_phys)) {
		X86_DEBUG_LOG(svm_intercept, LVL_ERR, "Failed to convert guest virtual 0x%"PRIADDR
		       " to guest physical.\n", context->vmcb->rip);
		return VMM_EFAIL;
	}

	/* FIXME: Should we always do cacheable memory access here ?? */
	if (vmm_guest_memory_read(guest, rip_phys, g_ins, sizeof(x86_inst),
				  TRUE) < sizeof(x86_inst)) {
		X86_DEBUG_LOG(svm_intercept, LVL_ERR, "Failed to read instruction at intercepted "
		       "instruction pointer. (0x%"PRIPADDR")\n", rip_phys);
		return VMM_EFAIL;
	}

	return VMM_OK;
}

static inline void dump_guest_exception_insts(struct vcpu_hw_context *context)
{
	x86_inst ins;
	int i;

	if (guest_read_fault_inst(context, &ins)) {
		X86_DEBUG_LOG(svm_intercept, LVL_ERR, "Failed to read faulting guest instruction.\n");
		return;
	}
	vmm_printf("\n");
	for (i = 0; i < 14; i++) {
		vmm_printf("%x ", ins[i]);
		if (i && !(i % 8)) vmm_printf("\n");
	}
	vmm_printf("\n");
}

/*
 * There can be 2 things here:
 * 1. Our shadow pagetable entry is stale
 *    This can be for 2 reasons:
 *       I. Linux has done lazy TLB where it has updated its page tables but
 *          didn't flush the TLB. There is no way for Xvisor to know that such
 *          thing has happened. We will handle it now.
 *       II. This is an actual fault that Linux wanted. The guest page in such
 *           case will be the same as ours. To handle this, a similar page fault
 *           with same reason should be injected in the guest. After this is
 *           done and the guest is resume it should update its page tables and
 *           it will resume from where it left. This will result in same page
 *           fault again because our shadow page table entries are still out of
 *           sync. But this time we will fall in reason III.
 *       III.
 *    a. Linux might have a different copy than ours.
 *       i. If so, update our page table entry and resume
 *          the guest.
 * 2. Our shadow pagetable entry is in sync with guest.
 *    a. Linux might have done a lazy TLB flush but
 */
static inline
void handle_guest_resident_page_fault(struct vcpu_hw_context *context)
{
	physical_addr_t fault_gphys = context->vmcb->exitinfo2;
	physical_addr_t lookedup_gphys;
	union page32 pte, pte1, pde, pde1;
	u32 prot, prot1, pdprot, pdprot1;

	X86_DEBUG_LOG(svm_intercept, LVL_DEBUG, "Resident page fault exit info 1: 0x%lx "
	       "2: 0x%lx rip: 0x%lx\n", context->vmcb->exitinfo1,
	       context->vmcb->exitinfo2, context->vmcb->rip);

	if (lookup_guest_pagetable(context, fault_gphys,
				   &lookedup_gphys, &pde, &pte)) {
		/* Lazy TLB flush by guest? */
		X86_DEBUG_LOG(svm_intercept, LVL_ERR, "ERROR: No entry in guest page table in "
		       "protection fault! Arrrgh! (Guest virtual: 0x%lx)\n",
		       fault_gphys);
		goto guest_bad_fault;
	}

	if (lookup_shadow_pagetable(context, fault_gphys,
				    &lookedup_gphys, &pde1, &pte1)) {
		X86_DEBUG_LOG(svm_intercept, LVL_ERR, "ERROR: No entry in shadow page table? "
		       "Arrrgh! (Guest virtual: 0x%lx)\n",
		       fault_gphys);
		goto guest_bad_fault;
	}

	prot = (pte._val & PGPROT_MASK);
	prot1 = (pte1._val & PGPROT_MASK);
	pdprot = (pde._val & PGPROT_MASK);
	pdprot1 = (pde1._val & PGPROT_MASK);

	if ((pdprot == pdprot1) && (prot == prot1)) {
		inject_guest_exception(context, VM_EXCEPTION_PAGE_FAULT);
		invalidate_guest_tlb(context, fault_gphys);
		return;
	}

	if (pdprot != pdprot1) {
		if (update_guest_shadow_pgprot(context, fault_gphys,
					       GUEST_PG_LVL_1, pdprot)
		    != VMM_OK) {
			X86_DEBUG_LOG(svm_intercept, LVL_ERR, "ERROR: Could not update level 2 "
			       "pgprot in shadow table(Guest virtual: 0x%lx)\n",
			       fault_gphys);
			goto guest_bad_fault;
		}
	}

	if (prot != prot1) {
		if (update_guest_shadow_pgprot(context, fault_gphys,
					       GUEST_PG_LVL_2, prot)
		    != VMM_OK) {
			X86_DEBUG_LOG(svm_intercept, LVL_ERR, "ERROR: Could not update level 1 "
			       "pgprot in shadow (Guest virtual: 0x%lx)\n",
			       fault_gphys);
			goto guest_bad_fault;
		}
	}

	/* invalidate guest TLB */
	invalidate_guest_tlb(context, fault_gphys);

	return;

 guest_bad_fault:
	if (context->vcpu_emergency_shutdown)
		context->vcpu_emergency_shutdown(context);
}

static inline
void handle_guest_realmode_page_fault(struct vcpu_hw_context *context,
				      physical_addr_t fault_gphys,
				      physical_size_t hphys_addr)
{
	if (create_guest_shadow_map(context, fault_gphys, hphys_addr,
				    PAGE_SIZE, 0x3, 0x3) != VMM_OK) {
		X86_DEBUG_LOG(svm_intercept, LVL_ERR, "ERROR: Failed to create map in"
		       "guest's shadow page table.\n"
		       "Fault Gphys: 0x%lx "
		       "Host Phys: %lx\n",
		       fault_gphys, hphys_addr);
		goto guest_bad_fault;
	}

	return;

guest_bad_fault:
	if (context->vcpu_emergency_shutdown)
		context->vcpu_emergency_shutdown(context);
}

static inline
void emulate_guest_mmio_io(struct vcpu_hw_context *context,
			   x86_decoded_inst_t *dinst)
{
	u64 guestrd;
	u64 index;
	physical_addr_t fault_gphys = context->vmcb->exitinfo2;

	if (gva_to_gpa(context,
		       dinst->inst.gen_mov.src_addr,
		       (physical_addr_t *)
		       &dinst->inst.gen_mov.src_addr) != VMM_OK) {
		X86_DEBUG_LOG(svm_intercept, LVL_ERR, "Failed to map guest va 0x%"PRIADDR" to pa\n",
		       dinst->inst.gen_mov.src_addr);
		goto guest_bad_fault;
	}

	if (vmm_devemu_emulate_read(context->assoc_vcpu,
				    fault_gphys,
				    &guestrd,
				    dinst->inst.gen_mov.op_size,
				    VMM_DEVEMU_NATIVE_ENDIAN) != VMM_OK) {
		vmm_printf("ERROR: Failed to emulate IO instruction in "
			   "guest.\n");
		goto guest_bad_fault;
	}

	/* update the guest register */
	if (dinst->inst.gen_mov.dst_addr >= RM_REG_AX &&
	    dinst->inst.gen_mov.dst_addr < RM_REG_MAX) {
		context->g_regs[dinst->inst.gen_mov.dst_addr] = guestrd;
		if (dinst->inst.gen_mov.dst_addr == RM_REG_AX)
			context->vmcb->rax = guestrd;
	} else {
		X86_DEBUG_LOG(svm_intercept, LVL_ERR, "Memory to memory move instruction not "
		       "supported.\n");
		goto guest_bad_fault;
	}

	index = dinst->inst.gen_mov.src_addr;
	if (dinst->inst.gen_mov.src_type == OP_TYPE_IMM) {
		guestrd = dinst->inst.gen_mov.src_addr;
	} else if (dinst->inst.gen_mov.src_addr >= RM_REG_AX &&
		dinst->inst.gen_mov.src_addr < RM_REG_MAX) {
		if (dinst->inst.gen_mov.dst_addr == RM_REG_AX)
			guestrd = context->vmcb->rax;
		else
			guestrd = context->g_regs[index];
	} else {
		X86_DEBUG_LOG(svm_intercept, LVL_ERR, "Memory to memory move instruction not "
		       "supported.\n");
		goto guest_bad_fault;
	}

	if (vmm_devemu_emulate_write(context->assoc_vcpu, fault_gphys,
				     &guestrd,
				     dinst->inst.gen_mov.op_size,
				     VMM_DEVEMU_NATIVE_ENDIAN) != VMM_OK) {
		vmm_printf("ERROR: Failed to emulate IO instruction in "
			   "guest.\n");
		goto guest_bad_fault;
	}

	return;

guest_bad_fault:
	if (context->vcpu_emergency_shutdown)
		context->vcpu_emergency_shutdown(context);
}

static inline
void handle_guest_mmio_fault(struct vcpu_hw_context *context)
{
	x86_inst ins;
	x86_decoded_inst_t dinst;

	if (guest_read_fault_inst(context, &ins)) {
		X86_DEBUG_LOG(svm_intercept, LVL_ERR, "Failed to read faulting guest instruction.\n");
		goto guest_bad_fault;
	}

	if (x86_decode_inst(context, ins, &dinst) != VMM_OK) {
		X86_DEBUG_LOG(svm_intercept, LVL_ERR, "Failed to decode guest instruction.\n");
		goto guest_bad_fault;
	}

	if (unlikely(dinst.inst_type != INST_TYPE_MOV)) {
		X86_DEBUG_LOG(svm_intercept, LVL_ERR,
		       "IO Fault in guest without a move instruction!\n");
		goto guest_bad_fault;
	}

	switch (dinst.inst_type) {
	case INST_TYPE_MOV:
		switch (dinst.inst.gen_mov.src_type) {
		case OP_TYPE_MEM:
			emulate_guest_mmio_io(context, &dinst);
		}
		break;
	default:
		goto guest_bad_fault;
	}

	context->vmcb->rip += dinst.inst_size;

	return;

 guest_bad_fault:
	if (context->vcpu_emergency_shutdown)
		context->vcpu_emergency_shutdown(context);
}

static inline
void handle_guest_protected_mem_rw(struct vcpu_hw_context *context)
{
	int rc;
	struct vmm_guest *guest = context->assoc_vcpu->guest;
	physical_addr_t fault_gphys = context->vmcb->exitinfo2;
	physical_addr_t hphys_addr;
	physical_size_t availsz;
	physical_addr_t lookedup_gphys;
	union page32 pte, pde;
	u32 flags, prot, pdprot;

	if (context->vmcb->exitinfo1 & 0x1) {
		handle_guest_resident_page_fault(context);
		return;
	}

	/* Guest has enabled paging, search for an entry
	 * in guest's page table.
	 */
	if (lookup_guest_pagetable(context, fault_gphys,
				   &lookedup_gphys, &pde, &pte)
	    != VMM_OK) {
		X86_DEBUG_LOG(svm_intercept, LVL_DEBUG, "ERROR: No page table entry "
		       "created by guest for fault address "
		       "0x%lx (rIP: 0x%lx)\n",
		       fault_gphys, context->vmcb->rip);
		X86_DEBUG_LOG(svm_intercept, LVL_DEBUG, "EXITINFO1: 0x%lx\n",
		       context->vmcb->exitinfo1);
		inject_guest_exception(context,
				       VM_EXCEPTION_PAGE_FAULT);
		return;
	}

	prot = (pte._val & PGPROT_MASK);
	pdprot = (pde._val & PGPROT_MASK);

	/*
	 * If page is present and marked readonly, page fault
	 * needs to be delivered to guest. This is because
	 * page protection flags are same as what guest had
	 * programmed.
	 */
	if ((PagePresent(&pte) && PageReadOnly(&pte))
	    ||(PagePresent(&pde) && PageReadOnly(&pde))) {
		if (!(context->vmcb->cr0 & (0x1UL << 16))) {
			X86_DEBUG_LOG(svm_intercept, LVL_ERR, "Page fault in guest "
			       "on valid page and WP unset.\n");
			goto guest_bad_fault;
		}
	}

	/* Find-out region mapping. */
	rc = vmm_guest_physical_map(guest, lookedup_gphys, PAGE_SIZE,
				    &hphys_addr, &availsz, &flags);
	if (rc)
		goto guest_bad_fault;
	if (availsz < PAGE_SIZE)
		goto guest_bad_fault;

	/*
	 * If fault is on a RAM backed address, map and return.
	 * Otherwise do emulate.
	 */
	if (flags & VMM_REGION_REAL) {
		if (create_guest_shadow_map(context, lookedup_gphys,
					    hphys_addr, PAGE_SIZE, pdprot,
					    prot) != VMM_OK) {
			X86_DEBUG_LOG(svm_intercept, LVL_ERR, "ERROR: Failed to create map in"
			       "guest's shadow page table.\n"
			       "Fault Gphys: 0x%lx "
			       "Lookup Gphys: 0x%lx "
			       "Host Phys: %lx\n",
			       fault_gphys, lookedup_gphys, hphys_addr);
			goto guest_bad_fault;
		}
	} else {
		handle_guest_mmio_fault(context);
	}

guest_bad_fault:
	if (context->vcpu_emergency_shutdown)
		context->vcpu_emergency_shutdown(context);
}

void __handle_vm_gdt_write(struct vcpu_hw_context *context)
{
	u64 gdt_entry;
	u32 guest_gdt_base = context->g_regs[GUEST_REGS_RBX];
	int i;

	vmm_printf("GDT Base: 0x%x\n", guest_gdt_base);
	for (i = 0; i < 2; i++) {
		guest_read_gva(context, guest_gdt_base, (u8 *)&gdt_entry,
			       sizeof(gdt_entry));
		vmm_printf("%2d : 0x%08lx\n", i, gdt_entry);
		guest_gdt_base += sizeof(gdt_entry);
	}

	if (context->vcpu_emergency_shutdown)
		context->vcpu_emergency_shutdown(context);
}

void __handle_vm_npf (struct vcpu_hw_context *context)
{
	X86_DEBUG_LOG(svm_intercept, LVL_INFO, "Unhandled Intercept: nested page fault.\n");
	if (context->vcpu_emergency_shutdown)
		context->vcpu_emergency_shutdown(context);
}

void __handle_vm_swint (struct vcpu_hw_context *context)
{
	X86_DEBUG_LOG(svm_intercept, LVL_INFO, "Unhandled Intercept: software interrupt.\n");
	if (context->vcpu_emergency_shutdown)
		context->vcpu_emergency_shutdown(context);
}

void __handle_vm_exception (struct vcpu_hw_context *context)
{
	struct vmm_guest *guest = context->assoc_vcpu->guest;

	switch (context->vmcb->exitcode)
	{
	case VMEXIT_EXCEPTION_PF:
		X86_DEBUG_LOG(svm_intercept, LVL_DEBUG, "Guest fault: 0x%"PRIx64" (rIP: %"PRIADDR")\n",
		       context->vmcb->exitinfo2, context->vmcb->rip);

		int rc;
		u32 flags;
		physical_addr_t fault_gphys = context->vmcb->exitinfo2;
		physical_addr_t hphys_addr;
		physical_size_t availsz;

		if (!(context->g_cr0 & X86_CR0_PG)) {
			/* Guest is in real mode so faulting guest virtual is
			 * guest physical address. We just need to add faulting
			 * address as offset to host physical address to get
			 * the destination physical address.
			 */
			rc = vmm_guest_physical_map(guest, fault_gphys, PAGE_SIZE,
						&hphys_addr, &availsz, &flags);
			if (rc) {
				X86_DEBUG_LOG(svm_intercept, LVL_ERR, "ERROR: No region mapped to "
				       "guest physical: 0x%lx\n", fault_gphys);
				goto guest_bad_fault;
			}
			if (availsz < PAGE_SIZE) {
				goto guest_bad_fault;
			}

			if (flags & (VMM_REGION_REAL | VMM_REGION_ALIAS)) {
				handle_guest_realmode_page_fault(context,
						fault_gphys, hphys_addr);
				return;
			}

			handle_guest_mmio_fault(context);
		} else {
			handle_guest_protected_mem_rw(context);
		}
		break;

	default:
		X86_DEBUG_LOG(svm_intercept, LVL_ERR, "Unhandled exception %s (rIP: 0x%"PRIADDR")\n",
		       exception_names[context->vmcb->exitcode - 0x40],
		       context->vmcb->rip);
		goto guest_bad_fault;
	}

	return;

 guest_bad_fault:
	if (context->vcpu_emergency_shutdown)
		context->vcpu_emergency_shutdown(context);
}

void __handle_vm_wrmsr (struct vcpu_hw_context *context)
{
	X86_DEBUG_LOG(svm_intercept, LVL_INFO, "Unhandled Intercept: msr write.\n");
	if (context->vcpu_emergency_shutdown)
		context->vcpu_emergency_shutdown(context);
}

void __handle_popf(struct vcpu_hw_context *context)
{
	X86_DEBUG_LOG(svm_intercept, LVL_INFO, "Unhandled Intercept: popf.\n");
	if (context->vcpu_emergency_shutdown)
		context->vcpu_emergency_shutdown(context);
}

void __handle_vm_vmmcall (struct vcpu_hw_context *context)
{
	X86_DEBUG_LOG(svm_intercept, LVL_INFO, "Unhandled Intercept: vmmcall.\n");
	if (context->vcpu_emergency_shutdown)
		context->vcpu_emergency_shutdown(context);
}

void __handle_vm_iret(struct vcpu_hw_context *context)
{
	X86_DEBUG_LOG(svm_intercept, LVL_INFO, "Unhandled Intercept: iret.\n");
	return;
}

void __handle_crN_read(struct vcpu_hw_context *context)
{
	int cr_gpr;

	/* Check if host support instruction decode assistance */
	if (context->cpuinfo->decode_assist) {
		if (context->vmcb->exitinfo1 & VALID_CRN_TRAP) {
			cr_gpr = (context->vmcb->exitinfo1 & 0xf);
			X86_DEBUG_LOG(svm_intercept, LVL_DEBUG, "Guest writing 0x%lx to Cr0 from reg"
			       "%d.\n",
			       context->g_regs[cr_gpr], cr_gpr);
		}
	} else {
		x86_inst ins64;
		x86_decoded_inst_t dinst;
		u64 rvalue;

		if (guest_read_fault_inst(context, &ins64)) {
			X86_DEBUG_LOG(svm_intercept, LVL_ERR, "Failed to read faulting guest "
			       "instruction.\n");
			goto guest_bad_fault;
		}

		if (x86_decode_inst(context, ins64, &dinst) != VMM_OK) {
			X86_DEBUG_LOG(svm_intercept, LVL_ERR, "Failed to decode instruction.\n");
			goto guest_bad_fault;
		}

		if (likely(dinst.inst_type == INST_TYPE_MOV_CR)) {
			switch (dinst.inst.crn_mov.src_reg) {
			case RM_REG_CR0:
				rvalue = context->g_cr0;
				break;

			case RM_REG_CR1:
				rvalue = context->g_cr1;
				break;

			case RM_REG_CR2:
				rvalue = context->g_cr2;
				break;

			case RM_REG_CR3:
				rvalue = context->g_cr3;
				break;

			case RM_REG_CR4:
				rvalue = context->g_cr4;
				break;

			default:
				X86_DEBUG_LOG(svm_intercept, LVL_ERR,
				       "Unknown CR 0x%"PRIx64" read by guest\n",
				       dinst.inst.crn_mov.src_reg);
				goto guest_bad_fault;
			}

			if (!dinst.inst.crn_mov.dst_reg)
				context->vmcb->rax = rvalue;

			context->g_regs[dinst.inst.crn_mov.dst_reg] = rvalue;
			context->vmcb->rip += dinst.inst_size;
			X86_DEBUG_LOG(svm_intercept, LVL_VERBOSE, "GR: CR0= 0x%8lx HCR0= 0x%8lx\n",
			       context->g_cr0, context->vmcb->cr0);
		} else {
			X86_DEBUG_LOG(svm_intercept, LVL_ERR, "Unknown fault inst: 0x%p\n",
			       ins64);
			goto guest_bad_fault;
		}
	}

	return;

 guest_bad_fault:
	if (context->vcpu_emergency_shutdown){
		context->vcpu_emergency_shutdown(context);
	}
}

void __handle_crN_write(struct vcpu_hw_context *context)
{
	int cr_gpr;
	u32 bits_clrd;
	u32 bits_set;
	u64 htr;
	u64 n_cr3;

	/* Check if host support instruction decode assistance */
	if (context->cpuinfo->decode_assist) {
		if (context->vmcb->exitinfo1 & VALID_CRN_TRAP) {
			cr_gpr = (context->vmcb->exitinfo1 & 0xf);
			X86_DEBUG_LOG(svm_intercept, LVL_DEBUG,
			       "Guest writing 0x%lx to Cr0 from reg "
			       "%d.\n",
			       context->g_regs[cr_gpr], cr_gpr);
		}
	} else {
		x86_inst ins64;
		x86_decoded_inst_t dinst;
		u32 sreg;

		if (guest_read_fault_inst(context, &ins64)) {
			X86_DEBUG_LOG(svm_intercept, LVL_ERR, "Failed to read guest instruction.\n");
			goto guest_bad_fault;
		}

		if (x86_decode_inst(context, ins64, &dinst) != VMM_OK) {
			X86_DEBUG_LOG(svm_intercept, LVL_ERR, "Failed to code instruction.\n");
			goto guest_bad_fault;
		}

		if (dinst.inst_type == INST_TYPE_MOV_CR) {
			switch(dinst.inst.crn_mov.dst_reg) {
			case RM_REG_CR0:
				if (!dinst.inst.crn_mov.src_reg) {
					bits_set = (~context->g_cr0
						    & context->vmcb->rax);
					bits_clrd = (context->g_cr0
						     & ~context->vmcb->rax);
					context->g_cr0 = context->vmcb->rax;
				} else {
					sreg = dinst.inst.crn_mov.src_reg;

					bits_set = (~context->g_cr0 &
						    context->g_regs[sreg]);
					bits_clrd = (context->g_cr0 &
						     ~context->g_regs[sreg]);
					context->g_cr0
						= context->g_regs[sreg];
				}

				if (bits_set & X86_CR0_PE) {
					context->vmcb->cr0 |= X86_CR0_PE;
				}

				if (bits_set & X86_CR0_PG) {
					context->vmcb->cr0 |= X86_CR0_PG;
					X86_DEBUG_LOG(svm_intercept, LVL_DEBUG,
					       "Purging guest shadow page "
					       "table.\n");
					purge_guest_shadow_pagetable(context);
				}

				if (bits_set & X86_CR0_AM) {
					context->vmcb->cr0 |= X86_CR0_AM;
				}

				if (bits_set & X86_CR0_MP) {
					context->vmcb->cr0 |= X86_CR0_MP;
				}

				if (bits_set & X86_CR0_WP) {
					context->vmcb->cr0 |= X86_CR0_WP;
				}

				if (bits_set & X86_CR0_CD) {
					context->vmcb->cr0 |= X86_CR0_CD;
				}

				if (bits_set & X86_CR0_NW) {
					context->vmcb->cr0 |= X86_CR0_NW;
				}

				/* Clear the required bits */
				if (bits_clrd & X86_CR0_PE) {
					context->vmcb->cr0 &= ~X86_CR0_PE;
				}

				if (bits_clrd & X86_CR0_PG) {
					context->vmcb->cr0 &= ~X86_CR0_PG;
				}

				if (bits_clrd & X86_CR0_AM) {
					context->vmcb->cr0 &= ~X86_CR0_AM;
				}

				if (bits_clrd & X86_CR0_MP) {
					context->vmcb->cr0 &= ~X86_CR0_MP;
				}

				if (bits_clrd & X86_CR0_WP) {
					context->vmcb->cr0 &= ~X86_CR0_WP;
				}

				if (bits_clrd & X86_CR0_CD) {
					context->vmcb->cr0 &= ~X86_CR0_CD;
				}

				if (bits_clrd & X86_CR0_NW) {
					context->vmcb->cr0 &= ~X86_CR0_NW;
				}

				break;

			case RM_REG_CR3:
				if (!dinst.inst.crn_mov.src_reg) {
					n_cr3 = context->vmcb->rax;
				} else {
					sreg = dinst.inst.crn_mov.src_reg;
					n_cr3 = context->g_regs[sreg];
				}

				/* Update only when there is a change in CR3 */
				if (likely(n_cr3 != context->g_cr3)) {
					context->g_cr3 = n_cr3;

					/* If the guest has paging enabled,
					   flush the shadow pagetable */
					if (likely(context->g_cr0
						   & X86_CR0_PG)) {
						X86_DEBUG_LOG(svm_intercept, LVL_DEBUG,
						       "Purging guest shadow "
						       "page table.\n");
						purge_guest_shadow_pagetable(context);
					}
				}
				break;

			case RM_REG_CR4:
				if (!dinst.inst.crn_mov.src_reg) {
					context->g_cr4 = context->vmcb->rax;
				} else {
					sreg = dinst.inst.crn_mov.src_reg;
					context->g_cr4 = context->g_regs[sreg];
				}
				X86_DEBUG_LOG(svm_intercept, LVL_DEBUG, "Guest wrote 0x%lx to CR4\n",
				       context->g_cr4);
				break;

			default:
				X86_DEBUG_LOG(svm_intercept, LVL_ERR,
				       "Write to CR%d not supported.\n",
				       (int)(dinst.inst.crn_mov.dst_reg - RM_REG_CR0));
				goto guest_bad_fault;
			}
		} else if (dinst.inst_type == INST_TYPE_CLR_CR) {
			switch(dinst.inst.crn_mov.dst_reg) {
			case RM_REG_CR0:
				context->g_cr0 &= ~(dinst.inst.crn_mov.src_reg);
				context->vmcb->cr0
					&= ~(dinst.inst.crn_mov.src_reg);
				break;
			}
		} else {
			X86_DEBUG_LOG(svm_intercept, LVL_ERR, "Unknown fault instruction\n");
			goto guest_bad_fault;
		}

		context->vmcb->rip += dinst.inst_size;

		asm volatile("str %0\n"
			     :"=r"(htr));
		X86_DEBUG_LOG(svm_intercept, LVL_DEBUG,
		       "GW: CR0= 0x%"PRIx64" HCR0: 0x%"PRIx64"\n",
		       context->g_cr0, context->vmcb->cr0);
		X86_DEBUG_LOG(svm_intercept, LVL_DEBUG,
		       "TR: 0x%"PRIx64" HTR: 0x%"PRIx64"\n",
		       *((u64 *)&context->vmcb->tr), htr);
	}

	return;

 guest_bad_fault:
	if (context->vcpu_emergency_shutdown){
		context->vcpu_emergency_shutdown(context);
	}
}

void __handle_ioio(struct vcpu_hw_context *context)
{
	u32 io_port = (context->vmcb->exitinfo1 >> 16);
	u8 in_inst = (context->vmcb->exitinfo1 & (0x1 << 0));
	u8 str_op = (context->vmcb->exitinfo1 & (0x1 << 2));
	u8 rep_access = (context->vmcb->exitinfo1 & (0x1 << 3));
	u8 op_size = (context->vmcb->exitinfo1 & (0x1 << 4) ? 8
		      : ((context->vmcb->exitinfo1 & (0x1 << 5)) ? 16
			 : 32));
	u8 seg_num = (context->vmcb->exitinfo1 >> 10) & 0x7;
	u32 guest_rd = 0;
	u32 wval;

	X86_DEBUG_LOG(svm_intercept, LVL_VERBOSE, "RIP: 0x%"PRIx64" exitinfo1: 0x%"PRIx64"\n",
	       context->vmcb->rip, context->vmcb->exitinfo1);
	X86_DEBUG_LOG(svm_intercept, LVL_VERBOSE,
	       "IOPort: 0x%x is accssed for %sput. Size is %d. Segment: %d "
	       "String operation? %s Repeated access? %s\n",
	       io_port, (in_inst ? "in" : "out"), op_size,
	       seg_num,(str_op ? "yes" : "no"),(rep_access ? "yes" : "no"));

	if (in_inst) {
		if (vmm_devemu_emulate_ioread(context->assoc_vcpu, io_port,
					      &guest_rd, op_size/8,
					      VMM_DEVEMU_NATIVE_ENDIAN)
		    != VMM_OK) {
			vmm_printf("Failed to emulate IO instruction in "
				   "guest.\n");
			goto _fail;
		}

		context->g_regs[GUEST_REGS_RAX] = guest_rd;
		context->vmcb->rax = guest_rd;
	} else {
		if (io_port == 0x80) {
			X86_DEBUG_LOG(svm_intercept, LVL_DEBUG, "(0x%"PRIx64") CBDW: 0x%"PRIx64"\n",
			       context->vmcb->rip, context->vmcb->rax);
		} else  {
			wval = (u32)context->vmcb->rax;
			if (vmm_devemu_emulate_iowrite(context->assoc_vcpu,
						       io_port,
						       &wval, op_size/8,
						       VMM_DEVEMU_NATIVE_ENDIAN)
			    != VMM_OK) {
				vmm_printf("Failed to emulate IO instruction in"
					   " guest.\n");
				goto _fail;
			}
		}
	}

	context->vmcb->rip = context->vmcb->exitinfo2;

	return;

 _fail:
	if (context->vcpu_emergency_shutdown){
		context->vcpu_emergency_shutdown(context);
	}
}

void __handle_cpuid(struct vcpu_hw_context *context)
{
	struct x86_vcpu_priv *priv = x86_vcpu_priv(context->assoc_vcpu);
	struct cpuid_response *func;

	switch (context->vmcb->rax) {
	case CPUID_BASE_LFUNCSTD:
		func = &priv->standard_funcs[CPUID_BASE_LFUNCSTD];
		context->vmcb->rax = func->resp_eax;
		context->g_regs[GUEST_REGS_RBX] = func->resp_ebx;
		context->g_regs[GUEST_REGS_RCX] = func->resp_ecx;
		context->g_regs[GUEST_REGS_RDX] = func->resp_edx;
		break;

	case CPUID_BASE_FEATURES:
		func = &priv->standard_funcs[CPUID_BASE_FEATURES];
		context->vmcb->rax = func->resp_eax;
		context->g_regs[GUEST_REGS_RBX] = func->resp_ebx;
		context->g_regs[GUEST_REGS_RCX] = func->resp_ecx;
		context->g_regs[GUEST_REGS_RDX] = func->resp_edx;
		break;

	case CPUID_EXTENDED_LFUNCEXTD:
	case CPUID_EXTENDED_BRANDSTRING:
	case CPUID_EXTENDED_BRANDSTRINGMORE:
	case CPUID_EXTENDED_BRANDSTRINGEND:
	case AMD_CPUID_EXTENDED_L1_CACHE_TLB_IDENTIFIER:
	case CPUID_EXTENDED_L2_CACHE_TLB_IDENTIFIER:
		func = &priv->extended_funcs[context->vmcb->rax
					     - CPUID_EXTENDED_LFUNCEXTD];
		context->vmcb->rax = func->resp_eax;
		context->g_regs[GUEST_REGS_RBX] = func->resp_ebx;
		context->g_regs[GUEST_REGS_RCX] = func->resp_ecx;
		context->g_regs[GUEST_REGS_RDX] = func->resp_edx;
		break;

	case CPUID_BASE_FEAT_FLAGS:
	case CPUID_EXTENDED_FEATURES:
	case AMD_CPUID_EXTENDED_ADDR_NR_PROC:
	case CPUID_BASE_PWR_MNG:
	case AMD_CPUID_EXTENDED_SVM_IDENTIFIER:
		context->vmcb->rax = 0;
		context->g_regs[GUEST_REGS_RBX] = 0;
		context->g_regs[GUEST_REGS_RCX] = 0;
		context->g_regs[GUEST_REGS_RDX] = 0;
		break;

	default:
		X86_DEBUG_LOG(svm_intercept, LVL_ERR, "GCPUID/R: Func: 0x%"PRIx64"\n",
		       context->vmcb->rax);
		goto _fail;
	}

	context->vmcb->rip += 2;

	return;

 _fail:
	if (context->vcpu_emergency_shutdown){
		context->vcpu_emergency_shutdown(context);
	}
}

/**
 * \brief Handle the shutdown condition in guest.
 *
 * If the guest has seen a shutdown condition (a.k.a. triple fault)
 * give the notification to guest and the guest must be
 * destroyed then. If the guest as multiple vCPUs, all of then
 * should be sent a notification of this.
 *
 * @param context
 * The hardware context of the vcpu of the guest which saw the triple fault.
 */
void __handle_triple_fault(struct vcpu_hw_context *context)
{
	X86_DEBUG_LOG(svm_intercept, LVL_ERR, "Triple fault in guest: %s!!\n",
	       context->assoc_vcpu->guest->name);

	if (context->vcpu_emergency_shutdown)
		context->vcpu_emergency_shutdown(context);

	vmm_hang();
}

void __handle_halt(struct vcpu_hw_context *context)
{
	X86_DEBUG_LOG(svm_intercept, LVL_INFO, "\n%s issued a halt instruction. Halting it.\n",
	       context->assoc_vcpu->guest->name);

	if (context->vcpu_emergency_shutdown)
		context->vcpu_emergency_shutdown(context);
}

void __handle_invalpg(struct vcpu_hw_context *context)
{
	u64 inval_va;
	x86_inst ins64;
	x86_decoded_inst_t dinst;

	if (guest_read_fault_inst(context, &ins64)) {
		X86_DEBUG_LOG(svm_intercept, LVL_ERR, "Failed to read guest instruction.\n");
		goto guest_bad_fault;
	}

	if (x86_decode_inst(context, ins64, &dinst) != VMM_OK) {
		X86_DEBUG_LOG(svm_intercept, LVL_ERR, "Failed to code instruction.\n");
		goto guest_bad_fault;
	}

	if (likely(dinst.inst_type == INST_TYPE_CACHE)) {
		inval_va = context->g_regs[dinst.inst.src_reg];

		invalidate_guest_tlb(context, inval_va);

		context->vmcb->rip += dinst.inst_size;
	}

	invalidate_shadow_entry(context, inval_va);

	return;

 guest_bad_fault:
	if (context->vcpu_emergency_shutdown)
		context->vcpu_emergency_shutdown(context);
}

void handle_vcpuexit(struct vcpu_hw_context *context)
{
	X86_DEBUG_LOG(svm_intercept, LVL_VERBOSE, "**** #VMEXIT - exit code: %x\n",
	       (u32) context->vmcb->exitcode);

	switch (context->vmcb->exitcode) {
	case VMEXIT_CR0_READ ... VMEXIT_CR15_READ:
		__handle_crN_read(context);
		break;

	case VMEXIT_CR0_WRITE ... VMEXIT_CR15_WRITE:
		__handle_crN_write(context);
		break;

	case VMEXIT_MSR:
		if (context->vmcb->exitinfo1 == 1)
			__handle_vm_wrmsr (context);
		break;

	case VMEXIT_EXCEPTION_DE ... VMEXIT_EXCEPTION_XF:
		__handle_vm_exception(context);
		break;

	case VMEXIT_SWINT:
		__handle_vm_swint(context);
		break;

	case VMEXIT_NPF:
		__handle_vm_npf (context);
		break;

	case VMEXIT_VMMCALL:
		__handle_vm_vmmcall(context);
		break;

	case VMEXIT_IRET:
		__handle_vm_iret(context);
		break;

	case VMEXIT_POPF:
		__handle_popf(context);
		break;

	case VMEXIT_SHUTDOWN:
		__handle_triple_fault(context);
		break;

	case VMEXIT_CPUID:
		__handle_cpuid(context);
		break;

	case VMEXIT_IOIO:
		__handle_ioio(context);
		break;

	case VMEXIT_GDTR_WRITE:
		__handle_vm_gdt_write(context);
		break;

	case VMEXIT_INTR:
		break; /* silently */

	case VMEXIT_HLT:
		__handle_halt(context);
		break;

	case VMEXIT_INVLPG:
		__handle_invalpg(context);
		break;

	case VMEXIT_VINTR:
		inject_guest_interrupt(context, 48);
		break;

	default:
		X86_DEBUG_LOG(svm_intercept, LVL_ERR, "#VMEXIT: Unhandled exit code: (0x%x:%d)\n",
		       (u32)context->vmcb->exitcode,
		       (u32)context->vmcb->exitcode);
		if (context->vcpu_emergency_shutdown)
			context->vcpu_emergency_shutdown(context);
	}
}

void svm_dump_guest_state(struct vcpu_hw_context *context)
{
	int i;
	u32 val;

	vmm_printf("RAX: 0x%"PRIx64" RBX: 0x%"PRIx64
		   " RCX: 0x%"PRIx64" RDX: 0x%"PRIx64"\n",
		   context->vmcb->rax, context->g_regs[GUEST_REGS_RBX],
		   context->g_regs[GUEST_REGS_RCX], context->g_regs[GUEST_REGS_RDX]);
	vmm_printf("R08: 0x%"PRIx64" R09: 0x%"PRIx64
		   " R10: 0x%"PRIx64" R11: 0x%"PRIx64"\n",
		   context->g_regs[GUEST_REGS_R8], context->g_regs[GUEST_REGS_R9],
		   context->g_regs[GUEST_REGS_R10], context->g_regs[GUEST_REGS_R10]);
	vmm_printf("R12: 0x%"PRIx64" R13: 0x%"PRIx64
		   " R14: 0x%"PRIx64" R15: 0x%"PRIx64"\n",
		   context->g_regs[GUEST_REGS_R12], context->g_regs[GUEST_REGS_R13],
		   context->g_regs[GUEST_REGS_R14], context->g_regs[GUEST_REGS_R15]);
	vmm_printf("RSP: 0x%"PRIx64" RBP: 0x%"PRIx64
		   " RDI: 0x%"PRIx64" RSI: 0x%"PRIx64"\n",
		   context->vmcb->rsp, context->g_regs[GUEST_REGS_RBP],
		   context->g_regs[GUEST_REGS_RDI], context->g_regs[GUEST_REGS_RSI]);
	vmm_printf("RIP: 0x%"PRIx64"\n\n", context->vmcb->rip);
	vmm_printf("CR0: 0x%"PRIx64" CR2: 0x%"PRIx64
		   " CR3: 0x%"PRIx64" CR4: 0x%"PRIx64"\n",
		   context->vmcb->cr0, context->vmcb->cr2,
		   context->vmcb->cr3, context->vmcb->cr4);

	dump_seg_selector("CS ", &context->vmcb->cs);
	dump_seg_selector("DS ", &context->vmcb->ds);
	dump_seg_selector("ES ", &context->vmcb->es);
	dump_seg_selector("SS ", &context->vmcb->ss);
	dump_seg_selector("FS ", &context->vmcb->fs);
	dump_seg_selector("GS ", &context->vmcb->gs);
	dump_seg_selector("GDT", &context->vmcb->gdtr);
	dump_seg_selector("LDT", &context->vmcb->ldtr);
	dump_seg_selector("IDT", &context->vmcb->idtr);
	dump_seg_selector("TR ", &context->vmcb->tr);


	vmm_printf("RFLAGS: 0x%"PRIx64"    [ ", context->vmcb->rflags);
	for (i = 0; i < 32; i++) {
		val = context->vmcb->rflags & (0x1UL << i);
		switch(val) {
		case X86_EFLAGS_CF:
			vmm_printf("CF ");
			break;
		case X86_EFLAGS_PF:
			vmm_printf("PF ");
			break;
		case X86_EFLAGS_AF:
			vmm_printf("AF ");
			break;
		case X86_EFLAGS_ZF:
			vmm_printf("ZF ");
			break;
		case X86_EFLAGS_SF:
			vmm_printf("SF ");
			break;
		case X86_EFLAGS_TF:
			vmm_printf("TF ");
			break;
		case X86_EFLAGS_IF:
			vmm_printf("IF ");
			break;
		case X86_EFLAGS_DF:
			vmm_printf("DF ");
			break;
		case X86_EFLAGS_OF:
			vmm_printf("OF ");
			break;
		case X86_EFLAGS_NT:
			vmm_printf("NT ");
			break;
		case X86_EFLAGS_RF:
			vmm_printf("RF ");
			break;
		case X86_EFLAGS_VM:
			vmm_printf("VM ");
			break;
		case X86_EFLAGS_AC:
			vmm_printf("AC ");
			break;
		case X86_EFLAGS_VIF:
			vmm_printf("VIF ");
			break;
		case X86_EFLAGS_VIP:
			vmm_printf("VIP ");
			break;
		case X86_EFLAGS_ID:
			vmm_printf("ID ");
			break;
		}
	}
}
