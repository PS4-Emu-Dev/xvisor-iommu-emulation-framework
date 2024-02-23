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
 * @file arch_vcpu.h
 * @author Anup Patel (anup@brainfault.org)
 * @brief generic interface for arch specific VCPU operations
 */
#ifndef _ARCH_VCPU_H__
#define _ARCH_VCPU_H__

#include <vmm_types.h>
#include <arch_regs.h>

struct vmm_chardev;
struct vmm_vcpu;

/** Architecture specific VCPU Initialization */
int arch_vcpu_init(struct vmm_vcpu *vcpu);

/** Architecture specific VCPU De-initialization (or Cleanup) */
int arch_vcpu_deinit(struct vmm_vcpu *vcpu);

/** VCPU context switch function
 *  NOTE: The tvcpu pointer is VCPU being switched out.
 *  NOTE: The vcpu pointer is VCPU being switched in.
 *  NOTE: This function is called with sched_lock held for
 *  both tvcpu and vcpu.
 *  NOTE: The pointer to arch_regs_t represents register state
 *  saved by interrupt handlers or arch_vcpu_preempt_orphan().
 */
void arch_vcpu_switch(struct vmm_vcpu *tvcpu,
		      struct vmm_vcpu *vcpu,
		      arch_regs_t *regs);

/** VCPU post context switch function
 *  NOTE: The vcpu pointer is VCPU being switched in.
 *  NOTE: The pointer to arch_regs_t represents register state
 *  saved by interrupt handlers or arch_vcpu_preempt_orphan().
 */
void arch_vcpu_post_switch(struct vmm_vcpu *vcpu,
			   arch_regs_t *regs);

/** Forcefully preempt current Orphan VCPU (or current Thread)
 *  NOTE: This functions is always called with irqs saved
 *  on the stack of current Orphan VCPU.
 *  NOTE: The core code expects that this function will save
 *  context and call vmm_scheduler_preempt_orphan() with a
 *  pointer to saved arch_regs_t.
 */
void arch_vcpu_preempt_orphan(void);

/** Print architecture specific registers of a VCPU */
void arch_vcpu_regs_dump(struct vmm_chardev *cdev, struct vmm_vcpu *vcpu);

/** Print architecture specific stats for a VCPU */
void arch_vcpu_stat_dump(struct vmm_chardev *cdev, struct vmm_vcpu *vcpu);

/** Get count of VCPU interrupts */
u32 arch_vcpu_irq_count(struct vmm_vcpu *vcpu);

/** Get priority for given VCPU interrupt number */
u32 arch_vcpu_irq_priority(struct vmm_vcpu *vcpu, u32 irq_no);

/** Assert VCPU interrupt
 *  NOTE: This function is called asynchronusly in any context.
 *  NOTE: This function needs to protect any common ressource that could
 *  be used concurently by other arch_vcpu_irq_xyz() functions.
 */
int arch_vcpu_irq_assert(struct vmm_vcpu *vcpu, u32 irq_no, u64 reason);

/** Check if we can execute multiple VCPU interrupts
 *  NOTE: This function is always called in context of the VCPU (i.e.
 *  in Normal context).
 *  NOTE: This function needs to protect any common ressource that could
 *  be used concurently by other arch_vcpu_irq_xyz() functions.
 */
bool arch_vcpu_irq_can_execute_multiple(struct vmm_vcpu *vcpu,
					arch_regs_t *regs);

/** Execute VCPU interrupt
 *  NOTE: This function is always called in context of the VCPU (i.e.
 *  in Normal context).
 *  NOTE: This function needs to protect any common ressource that could
 *  be used concurently by other arch_vcpu_irq_xyz() functions.
 */
int arch_vcpu_irq_execute(struct vmm_vcpu *vcpu,
			  arch_regs_t *regs,
			  u32 irq_no, u64 reason);

/** Force clear VCPU interrupt
 *  NOTE: This function is always called in context of the VCPU (i.e.
 *  in Normal context).
 *  NOTE: This function needs to protect any common ressource that could
 *  be used concurently by other arch_vcpu_irq_xyz() functions.
 */
int arch_vcpu_irq_clear(struct vmm_vcpu *vcpu, u32 irq_no, u64 reason);

/** Deassert VCPU interrupt
 *  NOTE: This function is called asynchronusly in any context.
 *  NOTE: This function needs to protect any common ressource that could
 *  be used concurently by other arch_vcpu_irq_xyz() functions.
 */
int arch_vcpu_irq_deassert(struct vmm_vcpu *vcpu, u32 irq_no, u64 reason);

/** VCPU IRQ pending status
 *  NOTE: This function is always called in context of the VCPU (i.e.
 *  in Normal context).
 *  NOTE: This function needs to protect any common ressource that could
 *  be used concurently by other arch_vcpu_irq_xyz() functions.
 */
bool arch_vcpu_irq_pending(struct vmm_vcpu *vcpu);

#endif
