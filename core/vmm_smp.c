/**
 * Copyright (c) 2013 Anup Patel.
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
 * @file vmm_smp.c
 * @author Anup Patel (anup@brainfault.org)
 * @brief Symetric Multiprocessor Mamagment APIs Implementation
 */

#include <vmm_error.h>
#include <vmm_limits.h>
#include <vmm_percpu.h>
#include <vmm_smp.h>
#include <vmm_cpuhp.h>
#include <vmm_delay.h>
#include <vmm_stdio.h>
#include <vmm_timer.h>
#include <vmm_completion.h>
#include <vmm_manager.h>
#include <libs/fifo.h>

/* SMP processor ID for Boot CPU */
static u32 smp_bootcpu_id = UINT_MAX;

int vmm_smp_map_cpuid(unsigned long hwid, u32 *cpu)
{
	u32 c;
	int rc;
	unsigned long thwid;

	if (!cpu)
		return VMM_EINVALID;

	for_each_possible_cpu(c) {
		rc = vmm_smp_map_hwid(c, &thwid);
		if (rc)
			return rc;

		if (thwid == hwid) {
			*cpu = c;
			return VMM_OK;
		}
	}

	return VMM_ENOENT;
}

u32 vmm_smp_bootcpu_id(void)
{
	return smp_bootcpu_id;
}

void vmm_smp_set_bootcpu(void)
{
	u32 cpu = vmm_smp_processor_id();

	if ((smp_bootcpu_id == UINT_MAX) &&
	    (cpu < CONFIG_CPU_COUNT)) {
		smp_bootcpu_id = cpu;
	}
}

bool vmm_smp_is_bootcpu(void)
{
	if (smp_bootcpu_id == UINT_MAX) {
		return FALSE;
	}

	return (smp_bootcpu_id == vmm_smp_processor_id()) ? TRUE : FALSE;
}

/* Theoretically, number of host CPUs making Sync IPI
 * simultaneously to a host CPU should not be more than
 * maximum possible hardware CPUs but, we keep minimum
 * Sync IPIs per host CPU to max possible VCPUs.
 */
#define SMP_IPI_MAX_SYNC_PER_CPU	(CONFIG_MAX_VCPU_COUNT)

/* Various trials show that having minimum of 64 Async IPI
 * per host CPU is good enough. If we require to increase
 * this limit then we should have a config option.
 */
#define SMP_IPI_MAX_ASYNC_PER_CPU	(64)

#define SMP_IPI_WAIT_TRY_COUNT		100
#define SMP_IPI_WAIT_UDELAY		1000

#define IPI_VCPU_STACK_SZ 		CONFIG_THREAD_STACK_SIZE
#define IPI_VCPU_PRIORITY 		VMM_VCPU_MAX_PRIORITY
#define IPI_VCPU_TIMESLICE 		VMM_VCPU_DEF_TIME_SLICE
#define IPI_VCPU_DEADLINE 		VMM_VCPU_DEF_DEADLINE
#define IPI_VCPU_PERIODICITY		VMM_VCPU_DEF_PERIODICITY

struct smp_ipi_call {
	u32 src_cpu;
	u32 dst_cpu;
	void (*func)(void *, void *, void *);
	void *arg0;
	void *arg1;
	void *arg2;
};

struct smp_ipi_ctrl {
	struct fifo *sync_fifo;
	struct fifo *async_fifo;
	struct vmm_completion async_avail;
	struct vmm_vcpu *async_vcpu;
};

static DEFINE_PER_CPU(struct smp_ipi_ctrl, ictl);

static void smp_ipi_sync_submit(struct smp_ipi_ctrl *ictlp,
				struct smp_ipi_call *ipic)
{
	int try;

	if (!ipic || !ipic->func) {
		return;
	}

	try = SMP_IPI_WAIT_TRY_COUNT;
	while (!fifo_enqueue(ictlp->sync_fifo, ipic, FALSE) && try) {
		arch_smp_ipi_trigger(vmm_cpumask_of(ipic->dst_cpu));
		vmm_udelay(SMP_IPI_WAIT_UDELAY);
		try--;
	}

	if (!try) {
		vmm_panic("CPU%d: IPI sync fifo full\n", ipic->dst_cpu);
	}

	arch_smp_ipi_trigger(vmm_cpumask_of(ipic->dst_cpu));
}

static void smp_ipi_async_submit(struct smp_ipi_ctrl *ictlp,
				 struct smp_ipi_call *ipic)
{
	int try;

	if (!ipic || !ipic->func) {
		return;
	}

	try = SMP_IPI_WAIT_TRY_COUNT;
	while (!fifo_enqueue(ictlp->async_fifo, ipic, FALSE) && try) {
		arch_smp_ipi_trigger(vmm_cpumask_of(ipic->dst_cpu));
		vmm_udelay(SMP_IPI_WAIT_UDELAY);
		try--;
	}

	if (!try) {
		vmm_panic("CPU%d: IPI async fifo full\n", ipic->dst_cpu);
	}

	arch_smp_ipi_trigger(vmm_cpumask_of(ipic->dst_cpu));
}

static void smp_ipi_main(void)
{
	struct smp_ipi_call ipic;
	struct smp_ipi_ctrl *ictlp = &this_cpu(ictl);

	while (1) {
		/* Wait for some IPI to be available */
		vmm_completion_wait(&ictlp->async_avail);

		/* Process async IPIs */
		while (fifo_dequeue(ictlp->async_fifo, &ipic)) {
			if (ipic.func) {
				ipic.func(ipic.arg0, ipic.arg1, ipic.arg2);
			}
		}
	}
}

void vmm_smp_ipi_exec(void)
{
	struct smp_ipi_call ipic;
	struct smp_ipi_ctrl *ictlp = &this_cpu(ictl);

	/* Process Sync IPIs */
	while (fifo_dequeue(ictlp->sync_fifo, &ipic)) {
		if (ipic.func) {
			ipic.func(ipic.arg0, ipic.arg1, ipic.arg2);
		}
	}

	/* Signal IPI available event */
	if (!fifo_isempty(ictlp->async_fifo)) {
		vmm_completion_complete(&ictlp->async_avail);
	}
}

void vmm_smp_ipi_async_call(const struct vmm_cpumask *dest,
			     void (*func)(void *, void *, void *),
			     void *arg0, void *arg1, void *arg2)
{
	u32 c, cpu = vmm_smp_processor_id();
	struct smp_ipi_call ipic;

	if (!dest || !func) {
		return;
	}

	for_each_cpu(c, dest) {
		if (c == cpu) {
			func(arg0, arg1, arg2);
		} else {
			if (!vmm_cpu_online(c)) {
				continue;
			}

			ipic.src_cpu = cpu;
			ipic.dst_cpu = c;
			ipic.func = func;
			ipic.arg0 = arg0;
			ipic.arg1 = arg1;
			ipic.arg2 = arg2;
			smp_ipi_async_submit(&per_cpu(ictl, c), &ipic);
		}
	}
}

int vmm_smp_ipi_sync_call(const struct vmm_cpumask *dest,
			   u32 timeout_msecs,
			   void (*func)(void *, void *, void *),
			   void *arg0, void *arg1, void *arg2)
{
	int rc = VMM_OK;
	u64 timeout_tstamp;
	u32 c, trig_count, cpu = vmm_smp_processor_id();
	struct vmm_cpumask trig_mask = VMM_CPU_MASK_NONE;
	struct smp_ipi_call ipic;
	struct smp_ipi_ctrl *ictlp;

	if (!dest || !func) {
		return VMM_EFAIL;
	}

	trig_count = 0;
	for_each_cpu(c, dest) {
		if (c == cpu) {
			func(arg0, arg1, arg2);
		} else {
			if (!vmm_cpu_online(c)) {
				continue;
			}

			ipic.src_cpu = cpu;
			ipic.dst_cpu = c;
			ipic.func = func;
			ipic.arg0 = arg0;
			ipic.arg1 = arg1;
			ipic.arg2 = arg2;
			smp_ipi_sync_submit(&per_cpu(ictl, c), &ipic);
			vmm_cpumask_set_cpu(c, &trig_mask);
			trig_count++;
		}
	}

	if (trig_count && timeout_msecs) {
		rc = VMM_ETIMEDOUT;
		timeout_tstamp = vmm_timer_timestamp();
		timeout_tstamp += (u64)timeout_msecs * 1000000ULL;
		while (vmm_timer_timestamp() < timeout_tstamp) {
			for_each_cpu(c, &trig_mask) {
				ictlp = &per_cpu(ictl, c);
				if (!fifo_avail(ictlp->sync_fifo)) {
					vmm_cpumask_clear_cpu(c, &trig_mask);
					trig_count--;
				}
			}

			if (!trig_count) {
				rc = VMM_OK;
				break;
			}

			vmm_udelay(SMP_IPI_WAIT_UDELAY);
		}
	}

	return rc;
}

static int smp_sync_ipi_startup(struct vmm_cpuhp_notify *cpuhp, u32 cpu)
{
	int rc = VMM_EFAIL;
	struct smp_ipi_ctrl *ictlp = &per_cpu(ictl, cpu);

	/* Initialize Sync IPI FIFO */
	ictlp->sync_fifo = fifo_alloc(sizeof(struct smp_ipi_call),
					   SMP_IPI_MAX_SYNC_PER_CPU);
	if (!ictlp->sync_fifo) {
		rc = VMM_ENOMEM;
		goto fail;
	}

	/* Initialize Async IPI FIFO */
	ictlp->async_fifo = fifo_alloc(sizeof(struct smp_ipi_call),
					   SMP_IPI_MAX_ASYNC_PER_CPU);
	if (!ictlp->async_fifo) {
		rc = VMM_ENOMEM;
		goto fail_free_sync;
	}

	/* Initialize IPI available completion event */
	INIT_COMPLETION(&ictlp->async_avail);

	/* Clear async VCPU pointer */
	ictlp->async_vcpu = NULL;

	/* Arch specific IPI initialization */
	if ((rc = arch_smp_ipi_init())) {
		goto fail_free_async;
	}

	return VMM_OK;

fail_free_async:
	fifo_free(ictlp->async_fifo);
fail_free_sync:
	fifo_free(ictlp->sync_fifo);
fail:
	return rc;
}

static struct vmm_cpuhp_notify smp_sync_ipi_cpuhp = {
	.name = "SMP_SYNC_IPI",
	.state = VMM_CPUHP_STATE_SMP_SYNC_IPI,
	.startup = smp_sync_ipi_startup,
};

int __init vmm_smp_sync_ipi_init(void)
{
	/* Setup hotplug notifier */
	return vmm_cpuhp_register(&smp_sync_ipi_cpuhp, TRUE);
}

static int smp_async_ipi_startup(struct vmm_cpuhp_notify *cpuhp, u32 cpu)
{
	int rc = VMM_EFAIL;
	char vcpu_name[VMM_FIELD_NAME_SIZE];
	struct smp_ipi_ctrl *ictlp = &per_cpu(ictl, cpu);

	/* Create IPI bottom-half VCPU. (Per Host CPU) */
	vmm_snprintf(vcpu_name, sizeof(vcpu_name), "ipi/%d", cpu);
	ictlp->async_vcpu = vmm_manager_vcpu_orphan_create(vcpu_name,
						(virtual_addr_t)&smp_ipi_main,
						IPI_VCPU_STACK_SZ,
						IPI_VCPU_PRIORITY,
						IPI_VCPU_TIMESLICE,
						IPI_VCPU_DEADLINE,
						IPI_VCPU_PERIODICITY,
						vmm_cpumask_of(cpu));
	if (!ictlp->async_vcpu) {
		rc = VMM_EFAIL;
		goto fail;
	}

	/* Kick IPI orphan VCPU */
	if ((rc = vmm_manager_vcpu_kick(ictlp->async_vcpu))) {
		goto fail_free_vcpu;
	}

	return VMM_OK;

fail_free_vcpu:
	vmm_manager_vcpu_orphan_destroy(ictlp->async_vcpu);
fail:
	return rc;
}

static struct vmm_cpuhp_notify smp_async_ipi_cpuhp = {
	.name = "SMP_ASYNC_IPI",
	.state = VMM_CPUHP_STATE_SMP_ASYNC_IPI,
	.startup = smp_async_ipi_startup,
};

int __init vmm_smp_async_ipi_init(void)
{
	/* Setup hotplug notifier */
	return vmm_cpuhp_register(&smp_async_ipi_cpuhp, TRUE);
}
