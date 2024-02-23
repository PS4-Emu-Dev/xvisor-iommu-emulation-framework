/**
 * Copyright (c) 2012 Anup Patel.
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
 * @file cpu_vcpu_irq.c
 * @author Anup Patel (anup@brainfault.org)
 * @brief source code for handling vcpu interrupts
 */

#include <vmm_error.h>
#include <vmm_scheduler.h>
#include <vmm_vcpu_irq.h>
#include <arch_vcpu.h>
#include <cpu_inline_asm.h>
#include <cpu_vcpu_inject.h>
#include <cpu_defines.h>

u32 arch_vcpu_irq_count(struct vmm_vcpu *vcpu)
{
	return CPU_IRQ_NR;
}

u32 arch_vcpu_irq_priority(struct vmm_vcpu *vcpu, u32 irq_no)
{
	u32 ret = 3;

	switch (irq_no) {
	case CPU_RESET_IRQ:
		ret = 0;
		break;
	case CPU_UNDEF_INST_IRQ:
		ret = 1;
		break;
	case CPU_SOFT_IRQ:
		ret = 2;
		break;
	case CPU_PREFETCH_ABORT_IRQ:
		ret = 2;
		break;
	case CPU_DATA_ABORT_IRQ:
		ret = 2;
		break;
	case CPU_HYP_TRAP_IRQ:
		ret = 2;
		break;
	case CPU_EXTERNAL_IRQ:
		ret = 2;
		break;
	case CPU_EXTERNAL_FIQ:
		ret = 2;
		break;
	default:
		break;
	};
	return ret;
}

int arch_vcpu_irq_assert(struct vmm_vcpu *vcpu, u32 irq_no, u64 reason)
{
	u32 hcr;
	bool update_hcr;
	irq_flags_t flags;

	/* Skip IRQ & FIQ if VGIC available */
	if (arm_vgic_avail(vcpu) &&
	    ((irq_no == CPU_EXTERNAL_IRQ) ||
	     (irq_no == CPU_EXTERNAL_FIQ))) {
		return VMM_OK;
	}

	vmm_spin_lock_irqsave_lite(&arm_priv(vcpu)->hcr_lock, flags);

	hcr = arm_priv(vcpu)->hcr;
	update_hcr = FALSE;

	switch(irq_no) {
	case CPU_EXTERNAL_IRQ:
		hcr |= HCR_VI_MASK;
		/* VI bit will be cleared on deassertion */
		update_hcr = TRUE;
		break;
	case CPU_EXTERNAL_FIQ:
		hcr |= HCR_VF_MASK;
		/* VF bit will be cleared on deassertion */
		update_hcr = TRUE;
		break;
	default:
		break;
	};

	if (update_hcr) {
		arm_priv(vcpu)->hcr = hcr;
		if (vmm_scheduler_current_vcpu() == vcpu) {
			write_hcr(hcr);
		}
	}

	vmm_spin_unlock_irqrestore_lite(&arm_priv(vcpu)->hcr_lock, flags);

	return VMM_OK;
}

bool arch_vcpu_irq_can_execute_multiple(struct vmm_vcpu *vcpu,
					arch_regs_t *regs)
{
	return FALSE;
}

int arch_vcpu_irq_execute(struct vmm_vcpu *vcpu,
			  arch_regs_t *regs,
			  u32 irq_no, u64 reason)
{
	int rc;
	irq_flags_t flags;

	/* Skip IRQ & FIQ if VGIC available */
	if (arm_vgic_avail(vcpu) &&
	    ((irq_no == CPU_EXTERNAL_IRQ) ||
	     (irq_no == CPU_EXTERNAL_FIQ))) {
		arm_vgic_irq_pending(vcpu);
		return VMM_OK;
	}

	/* Undefined, Data abort, and Prefetch abort
	 * can only be emulated in normal context.
	 */
	switch(irq_no) {
	case CPU_UNDEF_INST_IRQ:
		rc = cpu_vcpu_inject_undef(vcpu, regs);
		break;
	case CPU_PREFETCH_ABORT_IRQ:
		rc = cpu_vcpu_inject_pabt(vcpu, regs);
		break;
	case CPU_DATA_ABORT_IRQ:
		rc = cpu_vcpu_inject_dabt(vcpu, regs, (virtual_addr_t)reason);
		break;
	default:
		rc = VMM_OK;
		break;
	};

	/* Update HCR in HW */
	vmm_spin_lock_irqsave_lite(&arm_priv(vcpu)->hcr_lock, flags);
	write_hcr(arm_priv(vcpu)->hcr);
	vmm_spin_unlock_irqrestore_lite(&arm_priv(vcpu)->hcr_lock, flags);

	return rc;
}

int arch_vcpu_irq_clear(struct vmm_vcpu *vcpu, u32 irq_no, u64 reason)
{
	/* We don't implement this. */
	return VMM_OK;
}

int arch_vcpu_irq_deassert(struct vmm_vcpu *vcpu, u32 irq_no, u64 reason)
{
	u32 hcr;
	bool update_hcr;
	irq_flags_t flags;

	/* Skip IRQ & FIQ if VGIC available */
	if (arm_vgic_avail(vcpu) &&
	    ((irq_no == CPU_EXTERNAL_IRQ) ||
	     (irq_no == CPU_EXTERNAL_FIQ))) {
		return VMM_OK;
	}

	vmm_spin_lock_irqsave_lite(&arm_priv(vcpu)->hcr_lock, flags);

	hcr = arm_priv(vcpu)->hcr;
	update_hcr = FALSE;

	switch(irq_no) {
	case CPU_EXTERNAL_IRQ:
		hcr &= ~HCR_VI_MASK;
		update_hcr = TRUE;
		break;
	case CPU_EXTERNAL_FIQ:
		hcr &= ~HCR_VF_MASK;
		update_hcr = TRUE;
		break;
	default:
		break;
	};

	if (update_hcr) {
		arm_priv(vcpu)->hcr = hcr;
		if (vmm_scheduler_current_vcpu() == vcpu) {
			write_hcr(hcr);
		}
	}

	vmm_spin_unlock_irqrestore_lite(&arm_priv(vcpu)->hcr_lock, flags);

	return VMM_OK;
}

bool arch_vcpu_irq_pending(struct vmm_vcpu *vcpu)
{
	return arm_vgic_irq_pending(vcpu);
}
