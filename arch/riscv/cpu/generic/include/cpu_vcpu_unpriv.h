/**
 * Copyright (c) 2019 Western Digital Corporation or its affiliates.
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
 * @file cpu_vcpu_unpriv.h
 * @author Anup Patel (anup.patel@wdc.com)
 * @brief RISC-V hypervisor unprivileged access routines
 */

#ifndef __CPU_VCPU_UNPRIV_H__
#define __CPU_VCPU_UNPRIV_H__

#include <vmm_types.h>

struct cpu_vcpu_trap;

/* Low-level unpriv trap handler
 * Note: This trap handler clobber T0 and T1 registers
 * Note: This trap handler uses T0 as temporary register
 * Note: This trap handler expect T1 pointing to struct cpu_vcpu_trap
 */
void __cpu_vcpu_unpriv_trap_handler(void);

/* Read instruction from Guest memory
 * Note: This function should only be called from normal context
 */
unsigned long __cpu_vcpu_unpriv_read_insn(unsigned long guest_addr,
					  struct cpu_vcpu_trap *trap);

/* Read unsigned long from Guest memory
 * Note: This function should only be called from normal context
 */
unsigned long __cpu_vcpu_unpriv_read_ulong(unsigned long guest_addr,
					   struct cpu_vcpu_trap *trap);

#endif
