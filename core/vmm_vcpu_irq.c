/**
 * Copyright (c) 2010 Anup Patel.
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
 * @file vmm_vcpu_irq.c
 * @author Anup Patel (anup@brainfault.org)
 * @brief source code for vcpu irq processing
 */

#include <arch_vcpu.h>
#include <vmm_error.h>
#include <vmm_heap.h>
#include <vmm_stdio.h>
#include <vmm_timer.h>
#include <vmm_scheduler.h>
#include <vmm_devtree.h>
#include <vmm_vcpu_irq.h>
#include <libs/stringlib.h>

#define DEASSERTED	0
#define ASSERTED	1
#define PENDING		2

#define WFI_YIELD_THRESHOLD	100

static bool vcpu_irq_process_one(struct vmm_vcpu *vcpu, arch_regs_t *regs)
{
	/* Proceed only if we have pending execute */
	if (arch_atomic_dec_if_positive(&vcpu->irqs.execute_pending) >= 0) {
		int irq_no = -1;
		u32 i, tmp_prio, irq_count = vcpu->irqs.irq_count;
		u32 irq_prio = 0;

		/* Find the irq number to process */
		for (i = 0; i < irq_count; i++) {
			if (arch_atomic_read(&vcpu->irqs.irq[i].assert) ==
			    ASSERTED) {
				tmp_prio = arch_vcpu_irq_priority(vcpu, i);
				if (tmp_prio > irq_prio) {
					irq_no = i;
					irq_prio = tmp_prio;
				}
			}
		}
		if (irq_no == -1) {
			return FALSE;
		}

		/* If irq number found then execute it */
		if (arch_atomic_cmpxchg(&vcpu->irqs.irq[irq_no].assert,
					ASSERTED, PENDING) == ASSERTED) {
			if (arch_vcpu_irq_execute(vcpu, regs, irq_no,
			    	vcpu->irqs.irq[irq_no].reason) == VMM_OK) {
				arch_atomic_write(&vcpu->irqs.
						  irq[irq_no].assert,
						  DEASSERTED);
				arch_atomic64_inc(&vcpu->irqs.
						  execute_count);
			} else {
				/* arch_vcpu_irq_execute failed may be
				 * because VCPU was already processing
				 * a VCPU irq hence increment execute
				 * pending count to try next time.
				 */
				arch_atomic_inc(&vcpu->irqs.
						execute_pending);
				arch_atomic_write(&vcpu->irqs.
						  irq[irq_no].assert,
						  ASSERTED);
			}
		}

		return TRUE;
	}

	return FALSE;
}

void vmm_vcpu_irq_process(struct vmm_vcpu *vcpu, arch_regs_t *regs)
{
	/* For non-normal vcpu dont do anything */
	if (!vcpu || !vcpu->is_normal) {
		return;
	}

	/* If vcpu is not in interruptible state then dont do anything */
	if (!(vmm_manager_vcpu_get_state(vcpu) &
					VMM_VCPU_STATE_INTERRUPTIBLE)) {
		return;
	}

	/* Process VCPU interrupts */
	if (arch_vcpu_irq_can_execute_multiple(vcpu, regs)) {
		while (vcpu_irq_process_one(vcpu, regs)) ;
	} else {
		vcpu_irq_process_one(vcpu, regs);
	}
}

static void vcpu_irq_wfi_resume(struct vmm_vcpu *vcpu, void *data)
{
	irq_flags_t flags;
	bool try_vcpu_resume = FALSE;

	/* Lock VCPU WFI */
	vmm_spin_lock_irqsave_lite(&vcpu->irqs.wfi.lock, flags);

	/*
	 * If this function is called with data == NULL then
	 * it means wake-up event happened so we should clear
	 * the yield_count.
	 */
	if (!data) {
		vcpu->irqs.wfi.yield_count = 0;
	}

	/* If VCPU was in wfi state then update state. */
	if (vcpu->irqs.wfi.state) {
		try_vcpu_resume = TRUE;

		/* Clear wait for irq state */
		vcpu->irqs.wfi.state = FALSE;

		/* Stop wait for irq timeout event */
		vmm_timer_event_stop(vcpu->irqs.wfi.priv);
	}

	/* Unlock VCPU WFI */
	vmm_spin_unlock_irqrestore_lite(&vcpu->irqs.wfi.lock, flags);

	/* Case 1: try_vcpu_resume == TRUE
	 *   We directly resume vcpu using vmm_manager_vcpu_resume().
	 *   This can fail if vcpu is in READY or RUNNING state.
	 * Case 2: try_vcpu_resume == FALSE
	 *   We do nothing.
	 */
	if (try_vcpu_resume) {
		vmm_manager_vcpu_resume(vcpu);
	}
}

static void vcpu_irq_wfi_timeout(struct vmm_timer_event *ev)
{
	vmm_manager_vcpu_hcpu_func(ev->priv,
				   VMM_VCPU_STATE_INTERRUPTIBLE,
				   vcpu_irq_wfi_resume, ev, FALSE);
}

void vmm_vcpu_irq_assert(struct vmm_vcpu *vcpu, u32 irq_no, u64 reason)
{
	bool asserted = FALSE;

	/* For non-normal VCPU dont do anything */
	if (!vcpu || !vcpu->is_normal) {
		return;
	}

	/* If VCPU is not in interruptible state then dont do anything */
	if (!(vmm_manager_vcpu_get_state(vcpu) & VMM_VCPU_STATE_INTERRUPTIBLE)) {
		return;
	}

	/* Check irq number */
	if (irq_no > vcpu->irqs.irq_count) {
		return;
	}

	/* Assert the irq */
	if (arch_atomic_cmpxchg(&vcpu->irqs.irq[irq_no].assert,
				DEASSERTED, ASSERTED) == DEASSERTED) {
		if (arch_vcpu_irq_assert(vcpu, irq_no, reason) == VMM_OK) {
			vcpu->irqs.irq[irq_no].reason = reason;
			arch_atomic_inc(&vcpu->irqs.execute_pending);
			arch_atomic64_inc(&vcpu->irqs.assert_count);
			asserted = TRUE;
		} else {
			arch_atomic_write(&vcpu->irqs.irq[irq_no].assert,
					  DEASSERTED);
		}
	}

	/* Resume VCPU from wfi */
	if (asserted) {
		vmm_manager_vcpu_hcpu_func(vcpu,
					   VMM_VCPU_STATE_INTERRUPTIBLE,
					   vcpu_irq_wfi_resume, NULL, FALSE);
	}
}

void vmm_vcpu_irq_clear(struct vmm_vcpu *vcpu, u32 irq_no)
{
	/* For non-normal vcpu dont do anything */
	if (!vcpu || !vcpu->is_normal) {
		return;
	}

	/* Check irq number */
	if (irq_no > vcpu->irqs.irq_count) {
		return;
	}

	/* Ensure given VCPU is current VCPU */
	BUG_ON(vmm_scheduler_current_vcpu() != vcpu);

	/* Call arch specific deassert */
	if (arch_vcpu_irq_clear(vcpu, irq_no,
				vcpu->irqs.irq[irq_no].reason) == VMM_OK) {
		arch_atomic64_inc(&vcpu->irqs.clear_count);
	}

	/* Reset VCPU irq assert state */
	arch_atomic_write(&vcpu->irqs.irq[irq_no].assert, DEASSERTED);

	/* Ensure irq reason is zeroed */
	vcpu->irqs.irq[irq_no].reason = 0x0;
}

void vmm_vcpu_irq_deassert(struct vmm_vcpu *vcpu, u32 irq_no)
{
	/* For non-normal vcpu dont do anything */
	if (!vcpu || !vcpu->is_normal) {
		return;
	}

	/* Check irq number */
	if (irq_no > vcpu->irqs.irq_count) {
		return;
	}

	/* Call arch specific deassert */
	if (arch_vcpu_irq_deassert(vcpu, irq_no,
				   vcpu->irqs.irq[irq_no].reason) == VMM_OK) {
		arch_atomic64_inc(&vcpu->irqs.deassert_count);
	}

	/* Reset VCPU irq assert state */
	arch_atomic_write(&vcpu->irqs.irq[irq_no].assert, DEASSERTED);

	/* Ensure irq reason is zeroed */
	vcpu->irqs.irq[irq_no].reason = 0x0;
}

int vmm_vcpu_irq_wait_resume(struct vmm_vcpu *vcpu)
{
	/* Sanity Checks */
	if (!vcpu || !vcpu->is_normal) {
		return VMM_EFAIL;
	}

	/* Resume VCPU from wfi */
	return vmm_manager_vcpu_hcpu_func(vcpu,
					  VMM_VCPU_STATE_INTERRUPTIBLE,
					  vcpu_irq_wfi_resume, NULL, FALSE);
}

int vmm_vcpu_irq_wait_timeout(struct vmm_vcpu *vcpu, u64 nsecs)
{
	irq_flags_t flags;
	bool have_irq, try_vcpu_yield = FALSE, try_vcpu_pause = FALSE;

	/* Sanity Checks */
	if (!vcpu || !vcpu->is_normal) {
		return VMM_EFAIL;
	}

	/* Ensure given VCPU is current VCPU */
	BUG_ON(vmm_scheduler_current_vcpu() != vcpu);

	/* Check for pending interrupts */
	have_irq = arch_atomic_read(&vcpu->irqs.execute_pending) ||
		   arch_vcpu_irq_pending(vcpu);

	/* Lock VCPU WFI */
	vmm_spin_lock_irqsave_lite(&vcpu->irqs.wfi.lock, flags);

	/* Try to yield few times */
	if (have_irq) {
		vcpu->irqs.wfi.yield_count = 0;
		goto done;
	} else if (vcpu->irqs.wfi.yield_count < WFI_YIELD_THRESHOLD) {
		vcpu->irqs.wfi.yield_count++;
		try_vcpu_yield = TRUE;
		goto done;
	}

	if (!vcpu->irqs.wfi.state) {
		try_vcpu_pause = TRUE;

		/* Set wait for irq state */
		vcpu->irqs.wfi.state = TRUE;

		/* Start wait for irq timeout event */
		if (!nsecs) {
			nsecs = CONFIG_WFI_TIMEOUT_MSECS * 1000000ULL;
		}
		vmm_timer_event_start(vcpu->irqs.wfi.priv, nsecs);
	}

done:
	/* Unlock VCPU WFI */
	vmm_spin_unlock_irqrestore_lite(&vcpu->irqs.wfi.lock, flags);

	/* Try to yield the VCPU */
	if (try_vcpu_yield) {
		vmm_scheduler_yield();
	}

	/* Try to pause the VCPU */
	if (try_vcpu_pause) {
		/* Again check for pending interrupts */
		have_irq = arch_atomic_read(&vcpu->irqs.execute_pending) ||
			   arch_vcpu_irq_pending(vcpu);

		if (!have_irq) {
			/* Pause VCPU on WFI */
			vmm_manager_vcpu_pause(vcpu);
		} else {
			vmm_spin_lock_irqsave_lite(&vcpu->irqs.wfi.lock,
						   flags);

			/* Clear WFI yield count */
			vcpu->irqs.wfi.yield_count = 0;

			/* Clear wait for irq state */
			vcpu->irqs.wfi.state = FALSE;

			/* Stop wait for irq timeout event */
			vmm_timer_event_stop(vcpu->irqs.wfi.priv);

			vmm_spin_unlock_irqrestore_lite(&vcpu->irqs.wfi.lock,
							flags);
		}
	}

	return VMM_OK;
}

bool vmm_vcpu_irq_wait_state(struct vmm_vcpu *vcpu)
{
	bool ret = FALSE;
	irq_flags_t flags;

	/* Sanity Checks */
	if (!vcpu || !vcpu->is_normal) {
		return VMM_EFAIL;
	}

	/* Lock VCPU WFI */
	vmm_spin_lock_irqsave_lite(&vcpu->irqs.wfi.lock, flags);

	/* Read VCPU WFI state */
	ret = vcpu->irqs.wfi.state;

	/* Unlock VCPU WFI */
	vmm_spin_unlock_irqrestore_lite(&vcpu->irqs.wfi.lock, flags);

	return ret;
}

int vmm_vcpu_irq_init(struct vmm_vcpu *vcpu)
{
	int rc;
	u32 ite, irq_count;
	struct vmm_timer_event *ev;

	/* Sanity Checks */
	if (!vcpu) {
		return VMM_EFAIL;
	}

	/* For Orphan VCPU just return */
	if (!vcpu->is_normal) {
		return VMM_OK;
	}

	/* Get irq count */
	irq_count = arch_vcpu_irq_count(vcpu);

	/* Only first time */
	if (!vcpu->reset_count) {
		/* Clear the memory of irq */
		memset(&vcpu->irqs, 0, sizeof(struct vmm_vcpu_irqs));

		/* Allocate memory for flags */
		vcpu->irqs.irq =
		    vmm_zalloc(sizeof(struct vmm_vcpu_irq) * irq_count);
		if (!vcpu->irqs.irq) {
			return VMM_ENOMEM;
		}

		/* Create wfi_timeout event */
		ev = vmm_zalloc(sizeof(struct vmm_timer_event));
		if (!ev) {
			vmm_free(vcpu->irqs.irq);
			vcpu->irqs.irq = NULL;
			return VMM_ENOMEM;
		}
		vcpu->irqs.wfi.priv = ev;

		/* Initialize wfi lock */
		INIT_SPIN_LOCK(&vcpu->irqs.wfi.lock);

		/* Initialize wfi timeout event */
		INIT_TIMER_EVENT(ev, vcpu_irq_wfi_timeout, vcpu);
	}

	/* Save irq count */
	vcpu->irqs.irq_count = irq_count;

	/* Set execute pending to zero */
	arch_atomic_write(&vcpu->irqs.execute_pending, 0);

	/* Set default assert & deassert counts */
	arch_atomic64_write(&vcpu->irqs.assert_count, 0);
	arch_atomic64_write(&vcpu->irqs.execute_count, 0);
	arch_atomic64_write(&vcpu->irqs.clear_count, 0);
	arch_atomic64_write(&vcpu->irqs.deassert_count, 0);

	/* Reset irq processing data structures for VCPU */
	for (ite = 0; ite < irq_count; ite++) {
		vcpu->irqs.irq[ite].reason = 0;
		arch_atomic_write(&vcpu->irqs.irq[ite].assert, DEASSERTED);
	}

	/* Setup wait for irq context */
	vcpu->irqs.wfi.yield_count = 0;
	vcpu->irqs.wfi.state = FALSE;
	rc = vmm_timer_event_stop(vcpu->irqs.wfi.priv);
	if (rc != VMM_OK) {
		vmm_free(vcpu->irqs.irq);
		vcpu->irqs.irq = NULL;
		vmm_free(vcpu->irqs.wfi.priv);
		vcpu->irqs.wfi.priv = NULL;
	}

	return rc;
}

int vmm_vcpu_irq_deinit(struct vmm_vcpu *vcpu)
{
	/* Sanity Checks */
	if (!vcpu) {
		return VMM_EFAIL;
	}

	/* For Orphan VCPU just return */
	if (!vcpu->is_normal) {
		return VMM_OK;
	}

	/* Stop wfi_timeout event */
	vmm_timer_event_stop(vcpu->irqs.wfi.priv);

	/* Free wfi_timeout event */
	vmm_free(vcpu->irqs.wfi.priv);
	vcpu->irqs.wfi.priv = NULL;

	/* Free flags */
	vmm_free(vcpu->irqs.irq);
	vcpu->irqs.irq = NULL;

	return VMM_OK;
}
