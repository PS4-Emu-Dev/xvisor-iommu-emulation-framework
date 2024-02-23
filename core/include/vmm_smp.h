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
 * @file vmm_smp.h
 * @author Anup Patel (anup@brainfault.org)
 * @brief Symetric Multiprocessor Mamagment APIs
 */

#ifndef __VMM_SMP_H__
#define __VMM_SMP_H__

#include <vmm_types.h>
#include <vmm_error.h>
#include <vmm_cpumask.h>
#include <arch_smp.h>

/** Get SMP processor ID
 *  Note: To ease development, this function returns 0 on UP systems.
 */
#if !defined(CONFIG_SMP)
#define vmm_smp_processor_id()	0
#else
#define vmm_smp_processor_id()	arch_smp_id()
#endif

/** Get Hardware ID for given SMP processor ID
 *  Note: To ease development, this function returns 0 on UP systems.
 */
static inline int vmm_smp_map_hwid(u32 cpu, unsigned long *hwid)
{
	if (!hwid || !vmm_cpu_possible(cpu))
		return VMM_EINVALID;
#if !defined(CONFIG_SMP)
	*hwid = 0;
	return VMM_OK;
#else
	return arch_smp_map_hwid(cpu, hwid);
#endif
}

/** Get SMP processor ID for given Hardware ID
 *  Note: To ease development, this function returns 0 on UP systems.
 */
#if !defined(CONFIG_SMP)
static inline int vmm_smp_map_cpuid(unsigned long hwid, u32 *cpu)
{
	*cpu = 0;
	return VMM_OK;
}
#else
int vmm_smp_map_cpuid(unsigned long hwid, u32 *cpu);
#endif

/** Get SMP processor ID for Boot CPU
 *  Note: Boot CPU is the CPU on which we started booting.
 *  Note: To ease development, this function returns 0 on UP systems.
 */
#if !defined(CONFIG_SMP)
#define vmm_smp_bootcpu_id()	0
#else
u32 vmm_smp_bootcpu_id(void);
#endif

/** Set current SMP processor as Boot CPU
 *  Note: Boot CPU is the CPU on which we started booting.
 *  Note: This will work for first CPU calling this function and for
 *  subsequent CPUs it is ignored.
 *  Note: To ease development, this function does nothing on UP systems.
 */
#if !defined(CONFIG_SMP)
#define vmm_smp_set_bootcpu()
#else
void vmm_smp_set_bootcpu(void);
#endif

/** Check if current SMP processor is our Boot CPU.
 *  Note: Boot CPU is the CPU on which we started booting.
 *  Note: To ease development, this function returns TRUE on UP systems.
 */
#if !defined(CONFIG_SMP)
#define vmm_smp_is_bootcpu()	TRUE
#else
bool vmm_smp_is_bootcpu(void);
#endif

/** Execute IPI on current processor triggered by 
 *  some other processor
 *  Note: This is only available for SMP systems.
 *  Note: This functions has to be called by arch code upon
 *  getting an IPI interrupt.
 */
void vmm_smp_ipi_exec(void);

/** Asynchronus call to function on multiple cores
 *  Note: To ease development, we have dummy implementation for UP systems.
 */
#if !defined(CONFIG_SMP)
static inline
void vmm_smp_ipi_async_call(const struct vmm_cpumask *dest,
			    void (*func)(void *, void *, void *),
			    void *arg0, void *arg1, void *arg2)
{
	(func)(arg0, arg1, arg2);
}
#else
void vmm_smp_ipi_async_call(const struct vmm_cpumask *dest,
			    void (*func)(void *, void *, void *),
			    void *arg0, void *arg1, void *arg2);
#endif

/** Synchronus call to function on multiple cores
 *  Note: To ease development, we have dummy implementation for UP systems.
 */
#if !defined(CONFIG_SMP)
static inline
int vmm_smp_ipi_sync_call(const struct vmm_cpumask *dest,
			   u32 timeout_msecs,
			   void (*func)(void *, void *, void *),
			   void *arg0, void *arg1, void *arg2)
{
	(func)(arg0, arg1, arg2);
	return VMM_OK;
}
#else
int vmm_smp_ipi_sync_call(const struct vmm_cpumask *dest,
			   u32 timeout_msecs,
			   void (*func)(void *, void *, void *),
			   void *arg0, void *arg1, void *arg2);
#endif

/** Initialize SMP synchronus inter-processor interrupts
 *  Note: This has to be done only for SMP systems.
 */
int vmm_smp_sync_ipi_init(void);

/** Initialize SMP asynchronus inter-processor interrupts
 *  Note: This has to be done only for SMP systems.
 */
int vmm_smp_async_ipi_init(void);

#endif /* __VMM_SMP_H__ */
