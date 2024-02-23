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
 * @file vm.c
 * @author Himanshu Chauhan (hschauhan@nulltrace.org)
 * @brief Generic vm related code.
 */
#include <vmm_stdio.h>
#include <vmm_types.h>
#include <vmm_host_aspace.h>
#include <vmm_error.h>
#include <vmm_manager.h>
#include <libs/stringlib.h>
#include <cpu_mmu.h>
#include <cpu_vm.h>
#include <cpu_features.h>
#include <cpu_pgtbl_helper.h>
#include <arch_guest_helper.h>
#include <vm/svm.h>
#include <vm/svm_intercept.h>
#include <vm/vmx.h>
#include <vm/vmx_intercept.h>
#include <x86_debug_log.h>

DEFINE_X86_DEBUG_LOG_SUBSYS_LEVEL(x86_vm_helper, X86_DEBUG_LOG_LVL_INFO);

physical_addr_t cpu_create_vcpu_intercept_table(size_t size, virtual_addr_t *tbl_vaddr)
{
	physical_addr_t phys = 0;

	virtual_addr_t vaddr = vmm_host_alloc_pages(VMM_SIZE_TO_PAGE(size),
						    VMM_MEMORY_FLAGS_NORMAL);

	if (vmm_host_va2pa(vaddr, &phys) != VMM_OK)
		return 0;

	memset((void *)vaddr, 0x00, size);

	*tbl_vaddr = vaddr;

	return phys;
}

int cpu_free_vcpu_intercept_table(virtual_addr_t vaddr, size_t size)
{
	return vmm_host_free_pages(vaddr, VMM_SIZE_TO_PAGE(size));
}

void cpu_disable_vcpu_intercept(struct vcpu_hw_context *context, int flags)
{
	/* disable taskswitch interception */
	if (flags & USER_ITC_TASKSWITCH) {
		X86_DEBUG_LOG(x86_vm_helper, LVL_INFO, "Disable taskswitch interception\n");
		context->vmcb->cr_intercepts &= ~INTRCPT_WRITE_CR3;
	}

	if (flags & USER_ITC_SWINT) {
		X86_DEBUG_LOG(x86_vm_helper, LVL_INFO, "Disable software interrupt interception\n");
		context->vmcb->general1_intercepts &= ~INTRCPT_INTN;
	}

	if (flags & USER_ITC_IRET) {
		X86_DEBUG_LOG(x86_vm_helper, LVL_INFO, "Enable software interrupt interception\n");
		context->vmcb->general1_intercepts &= ~INTRCPT_IRET;
	}

	if (flags & USER_ITC_SYSCALL) {
		X86_DEBUG_LOG(x86_vm_helper, LVL_INFO, "Disable syscall interception\n");
		context->vmcb->general1_intercepts &= ~INTRCPT_INTN;
	}

	/* disable single stepping */
	if (flags & USER_SINGLE_STEPPING) {
		X86_DEBUG_LOG(x86_vm_helper, LVL_INFO, "Disable single stepping\n");
		context->vmcb->rflags &= ~X86_EFLAGS_TF;
		context->vmcb->exception_intercepts &= ~INTRCPT_EXC_DB;
	}
}

void cpu_enable_vcpu_intercept(struct vcpu_hw_context *context, int flags)
{
	/* enable taskswitch interception */
	if (flags & USER_ITC_TASKSWITCH) {
		X86_DEBUG_LOG(x86_vm_helper, LVL_INFO, "Enable taskswitch interception\n");
		context->vmcb->cr_intercepts |= INTRCPT_WRITE_CR3;
	}

	/* enable software interrupt interception */
	if (flags & USER_ITC_SWINT) {
		X86_DEBUG_LOG(x86_vm_helper, LVL_INFO, "Enable software interrupt interception\n");
		context->vmcb->general1_intercepts |= INTRCPT_INTN;
	}

	if (flags & USER_ITC_IRET) {
		X86_DEBUG_LOG(x86_vm_helper, LVL_INFO, "Enable software interrupt interception\n");
		context->vmcb->general1_intercepts |= INTRCPT_IRET;
	}
}

void enable_ioport_intercept(struct vcpu_hw_context *context, u32 ioport)
{
	u32 byte_offset = ioport >> 3;
	u8 port_offset = ioport & 0x7;

	u8 *iop_base = (u8 *)(context->icept_table.io_table_virt + byte_offset);
	*iop_base |= (0x1 << port_offset);
}

void disable_ioport_intercept(struct vcpu_hw_context *context, u32 ioport)
{
	u32 byte_offset = ioport >> 3;
	u8 port_offset = ioport & 0x7;

	u8 *iop_base = (u8 *)context->icept_table.io_table_virt + byte_offset;
	*iop_base &= ~(0x1 << port_offset);
}

int cpu_init_vcpu_hw_context(struct cpuinfo_x86 *cpuinfo,
			     struct vcpu_hw_context *context)
{
	int ret = VMM_EFAIL;
	int boffs;

	context->cpuinfo = cpuinfo;

	context->shadow_pgt = mmu_pgtbl_alloc(&host_pgtbl_ctl, PGTBL_STAGE_2);
	if (!context->shadow_pgt) {
		X86_DEBUG_LOG(x86_vm_helper, LVL_DEBUG, "ERROR: Failed to allocate shadow page table for vcpu.\n");
		goto _error;
	}

	context->shadow32_pg_list = (union page32 *)vmm_host_alloc_pages(NR_32BIT_PGLIST_PAGES,
									 VMM_MEMORY_FLAGS_NORMAL);

	if (!context->shadow32_pg_list) {
		X86_DEBUG_LOG(x86_vm_helper, LVL_ERR, "ERROR: Failed to allocated 32bit/paged real mode shadow table.\n");
		goto _error;
	}

	memset(context->shadow32_pg_list, 0, (NR_32BIT_PGLIST_PAGES*PAGE_SIZE));

	/* Mark all pages in list free */
	bitmap_zero(context->shadow32_pg_map, NR_32BIT_PGLIST_PAGES);
	boffs = bitmap_find_free_region(context->shadow32_pg_map, NR_32BIT_PGLIST_PAGES, 0);
	context->shadow32_pgt = context->shadow32_pg_list + boffs;
	memset(context->shadow32_pgt, 0, PAGE_SIZE);
	context->pgmap_free_cache = boffs+1;

	context->icept_table.io_table_phys =
		cpu_create_vcpu_intercept_table(IO_INTCPT_TBL_SZ,
						&context->icept_table.io_table_virt);
	if (!context->icept_table.io_table_phys) {
		X86_DEBUG_LOG(x86_vm_helper, LVL_ERR, "ERROR: Failed to create I/O intercept table\n");
		goto _error;
	}

	context->icept_table.msr_table_phys =
		cpu_create_vcpu_intercept_table(MSR_INTCPT_TBL_SZ,
						&context->icept_table.msr_table_virt);
	if (!context->icept_table.msr_table_phys) {
		X86_DEBUG_LOG(x86_vm_helper, LVL_ERR, "ERROR: Failed to create MSR intercept table for vcpu.\n");
		goto _error;
	}

	switch (cpuinfo->vendor) {
	case x86_VENDOR_AMD:
		if((ret = amd_setup_vm_control(context)) != VMM_OK) {
			X86_DEBUG_LOG(x86_vm_helper, LVL_ERR, "ERROR: Failed to setup VM control.\n");
			goto _error;
		}
		break;

	case x86_VENDOR_INTEL:
		if ((ret = intel_setup_vm_control(context)) != VMM_OK) {
			X86_DEBUG_LOG(x86_vm_helper, LVL_ERR, "ERROR: Failed to setup vm control.\n");
			goto _error;
		}
		break;

	default:
		X86_DEBUG_LOG(x86_vm_helper, LVL_ERR, "ERROR: Invalid vendor %d\n", cpuinfo->vendor);
		goto _error;
		break;
	}

	return VMM_OK;

 _error:
	if (context->shadow32_pg_list) vmm_host_free_pages((virtual_addr_t)context->shadow32_pg_list,
							   NR_32BIT_PGLIST_PAGES*PAGE_SIZE);
	if (context->shadow_pgt) mmu_pgtbl_free(&host_pgtbl_ctl, context->shadow_pgt);

	if (context->icept_table.io_table_virt)
		cpu_free_vcpu_intercept_table(context->icept_table.io_table_virt, IO_INTCPT_TBL_SZ);

	if (context->icept_table.msr_table_virt)
		cpu_free_vcpu_intercept_table(context->icept_table.msr_table_virt, MSR_INTCPT_TBL_SZ);

	/* FIXME: VM: Free nested page table pages. */
	return VMM_EFAIL;
}

/*
 * Identify the CPU and enable the VM feature on it.
 * Only AMD processors are supported right now.
 */
int cpu_enable_vm_extensions(struct cpuinfo_x86 *cpuinfo)
{
	int ret = VMM_EFAIL;;

	switch (cpuinfo->vendor) {
	case x86_VENDOR_AMD:
		X86_DEBUG_LOG(x86_vm_helper, LVL_VERBOSE, "Initializing SVM on AMD.\n");
		ret = amd_init(cpuinfo);
		break;

	case x86_VENDOR_INTEL:
		X86_DEBUG_LOG(x86_vm_helper, LVL_VERBOSE, "Initializing VMX on Intel.\n");
		ret = intel_init(cpuinfo);
		break;

	case x86_VENDOR_UNKNOWN:
	default:
		vmm_panic("Unknown CPU vendor: %d\n", cpuinfo->vendor);
		break;
	}

	return ret;
}

void cpu_boot_vcpu(struct vcpu_hw_context *context)
{
	for(;;)	{
		context->vcpu_run(context);
	}
}
