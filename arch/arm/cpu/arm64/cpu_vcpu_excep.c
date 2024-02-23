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
 * @file cpu_vcpu_excep.c
 * @author Anup Patel (anup@brainfault.org)
 * @brief Source file for VCPU exception handling
 */

#include <vmm_error.h>
#include <vmm_stdio.h>
#include <vmm_host_aspace.h>
#include <vmm_guest_aspace.h>
#include <libs/stringlib.h>
#include <generic_mmu.h>

#include <cpu_inline_asm.h>
#include <cpu_vcpu_helper.h>
#include <cpu_vcpu_emulate.h>
#include <cpu_vcpu_excep.h>
#include <emulate_arm.h>
#include <emulate_thumb.h>

static int cpu_vcpu_stage2_map(struct vmm_vcpu *vcpu,
			       arch_regs_t *regs,
			       physical_addr_t fipa)
{
	int rc, rc1;
	u32 reg_flags = 0x0, pg_reg_flags = 0x0;
	struct mmu_page pg;
	physical_addr_t inaddr, outaddr;
	physical_size_t size, availsz;

	memset(&pg, 0, sizeof(pg));

	inaddr = fipa & TTBL_L3_MAP_MASK;
	size = TTBL_L3_BLOCK_SIZE;

	rc = vmm_guest_physical_map(vcpu->guest, inaddr, size,
				    &outaddr, &availsz, &reg_flags);
	if (rc) {
		vmm_printf("%s: IPA=0x%lx size=0x%lx map failed\n",
			   __func__, inaddr, size);
		return rc;
	}

	if (availsz < TTBL_L3_BLOCK_SIZE) {
		vmm_printf("%s: availsz=0x%lx insufficent for IPA=0x%lx\n",
			   __func__, availsz, inaddr);
		return VMM_EFAIL;
	}

	pg.ia = inaddr;
	pg.sz = size;
	pg.oa = outaddr;
	pg_reg_flags = reg_flags;

	if (reg_flags & (VMM_REGION_ISRAM | VMM_REGION_ISROM)) {
		inaddr = fipa & TTBL_L2_MAP_MASK;
		size = TTBL_L2_BLOCK_SIZE;
		rc = vmm_guest_physical_map(vcpu->guest, inaddr, size,
				    &outaddr, &availsz, &reg_flags);
		if (!rc && (availsz >= TTBL_L2_BLOCK_SIZE)) {
			pg.ia = inaddr;
			pg.sz = size;
			pg.oa = outaddr;
			pg_reg_flags = reg_flags;
		}
		inaddr = fipa & TTBL_L1_MAP_MASK;
		size = TTBL_L1_BLOCK_SIZE;
		rc = vmm_guest_physical_map(vcpu->guest, inaddr, size,
				    &outaddr, &availsz, &reg_flags);
		if (!rc && (availsz >= TTBL_L1_BLOCK_SIZE)) {
			pg.ia = inaddr;
			pg.sz = size;
			pg.oa = outaddr;
			pg_reg_flags = reg_flags;
		}
	}

	arch_mmu_pgflags_set(&pg.flags, MMU_STAGE2, pg_reg_flags);

	/* Try to map the page in Stage2 */
	rc = mmu_map_page(arm_guest_priv(vcpu->guest)->ttbl, &pg);
	if (rc) {
		/* On SMP Guest, two different VCPUs may try to map same
		 * Guest region in Stage2 at the same time. This may cause
		 * mmu_lpae_map_page() to fail for one of the Guest VCPUs.
		 *
		 * To take care of this situation, we recheck Stage2 mapping
		 * when mmu_lpae_map_page() fails.
		 */
		memset(&pg, 0, sizeof(pg));
		rc1 = mmu_get_page(arm_guest_priv(vcpu->guest)->ttbl,
				   fipa, &pg);
		if (rc1) {
			return rc1;
		}
		rc = VMM_OK;
	}

	return rc;
}

int cpu_vcpu_inst_abort(struct vmm_vcpu *vcpu,
			arch_regs_t *regs,
			u32 il, u32 iss,
			physical_addr_t fipa)
{
	switch (iss & ISS_ABORT_FSC_MASK) {
	case FSC_TRANS_FAULT_LEVEL1:
	case FSC_TRANS_FAULT_LEVEL2:
	case FSC_TRANS_FAULT_LEVEL3:
		return cpu_vcpu_stage2_map(vcpu, regs, fipa);
	default:
		break;
	};

	return VMM_EFAIL;
}

int cpu_vcpu_data_abort(struct vmm_vcpu *vcpu,
			arch_regs_t *regs,
			u32 il, u32 iss,
			physical_addr_t fipa)
{
	u32 read_count, inst;
	physical_addr_t inst_pa;

	switch (iss & ISS_ABORT_FSC_MASK) {
	case FSC_TRANS_FAULT_LEVEL1:
	case FSC_TRANS_FAULT_LEVEL2:
	case FSC_TRANS_FAULT_LEVEL3:
		return cpu_vcpu_stage2_map(vcpu, regs, fipa);
	case FSC_ACCESS_FAULT_LEVEL1:
	case FSC_ACCESS_FAULT_LEVEL2:
	case FSC_ACCESS_FAULT_LEVEL3:
		if (!(iss & ISS_ABORT_ISV_MASK)) {
			/* Determine instruction physical address */
			va2pa_at(VA2PA_STAGE12, VA2PA_EL1, VA2PA_RD, regs->pc);
			inst_pa = mrs(par_el1);
			inst_pa &= PAR_PA_MASK;
			inst_pa |= (regs->pc & 0x00000FFF);

			/* Read the faulting instruction */
			/* FIXME: Should this be cacheable memory access ? */
			read_count = vmm_host_memory_read(inst_pa,
						&inst, sizeof(inst), TRUE);
			if (read_count != sizeof(inst)) {
				return VMM_EFAIL;
			}
			if (regs->pstate & PSR_THUMB_ENABLED) {
				return emulate_thumb_inst(vcpu, regs, inst);
			} else {
				return emulate_arm_inst(vcpu, regs, inst);
			}
		}
		if (iss & ISS_ABORT_WNR_MASK) {
			return cpu_vcpu_emulate_store(vcpu, regs,
						      il, iss, fipa);
		} else {
			return cpu_vcpu_emulate_load(vcpu, regs,
						     il, iss, fipa);
		}
	default:
		vmm_printf("%s: Unhandled FSC=0x%x\n",
			   __func__, iss & ISS_ABORT_FSC_MASK);
		break;
	};

	return VMM_EFAIL;
}

