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
 * @file vmm_manager.h
 * @author Anup Patel (anup@brainfault.org)
 * @brief header file for hypervisor manager
 */
#ifndef _VMM_MANAGER_H__
#define _VMM_MANAGER_H__

#include <arch_regs.h>
#include <arch_atomic64.h>
#include <arch_atomic.h>
#include <vmm_limits.h>
#include <vmm_types.h>
#include <vmm_spinlocks.h>
#include <vmm_devtree.h>
#include <vmm_cpumask.h>
#include <vmm_shmem.h>
#include <libs/list.h>
#include <libs/rbtree.h>

enum vmm_region_flags {
	VMM_REGION_REAL=0x00000001,
	VMM_REGION_VIRTUAL=0x00000002,
	VMM_REGION_ALIAS=0x00000004,
	VMM_REGION_MEMORY=0x00000008,
	VMM_REGION_IO=0x00000010,
	VMM_REGION_CACHEABLE=0x00000020,
	VMM_REGION_BUFFERABLE=0x00000040,
	VMM_REGION_READONLY=0x00000080,
	VMM_REGION_ISRAM=0x00000100,
	VMM_REGION_ISROM=0x00000200,
	VMM_REGION_ISDEVICE=0x00000400,
	VMM_REGION_ISRESERVED=0x00000800,
	VMM_REGION_ISALLOCED=0x00001000,
	VMM_REGION_ISCOLORED=0x00002000,
	VMM_REGION_ISSHARED=0x00004000,
	VMM_REGION_ISDYNAMIC=0x00008000,
};

#define VMM_REGION_MANIFEST_MASK	(VMM_REGION_REAL | \
					 VMM_REGION_VIRTUAL | \
					 VMM_REGION_ALIAS)

enum vmm_region_mapping_flags {
	VMM_REGION_MAPPING_ISHOSTRAM=0x00000001,
};

struct vmm_region;
struct vmm_region_mapping;
struct vmm_guest_aspace;
struct vmm_vcpu_irqs;
struct vmm_vcpu;
struct vmm_guest;

struct vmm_region_mapping {
	physical_addr_t hphys_addr;
	u32 flags;
};

struct vmm_region {
	struct rb_node head;
	struct dlist phead;
	struct vmm_devtree_node *node;
	struct vmm_guest_aspace *aspace;
	u32 flags;
	physical_addr_t gphys_addr;
	physical_addr_t aphys_addr;
	physical_size_t phys_size;
	u32 first_color;
	u32 num_colors;
	struct vmm_shmem *shm;
	u32 align_order;
	u32 map_order;
	u32 maps_count;
	struct vmm_region_mapping *maps;
	void *devemu_priv;
	void *priv;
};

#define VMM_REGION_NAME(reg)		((reg)->node->name)
#define VMM_REGION_GPHYS_START(reg)	((reg)->gphys_addr)
#define VMM_REGION_GPHYS_END(reg)	((reg)->gphys_addr + (reg)->phys_size)
#define VMM_REGION_PHYS_SIZE(reg)	((reg)->phys_size)
#define VMM_REGION_GPHYS_TO_APHYS(reg, gphys)	\
			((reg)->aphys_addr + ((gphys) - (reg)->gphys_addr))
#define VMM_REGION_FLAGS(reg)		((reg)->flags)
#define VMM_REGION_ALIGN_ORDER(reg)	((reg)->align_order)
#define VMM_REGION_MAP_ORDER(reg)	((reg)->map_order)
#define VMM_REGION_MAPS_COUNT(reg)	((reg)->maps_count)

struct vmm_guest_aspace {
	struct vmm_devtree_node *node;
	struct vmm_guest *guest;
	bool initialized;
	vmm_rwlock_t reg_iotree_lock;
	struct rb_root reg_iotree;
	struct dlist reg_ioprobe_list;
	vmm_rwlock_t reg_memtree_lock;
	struct rb_root reg_memtree;
	struct dlist reg_memprobe_list;
	void *devemu_priv;
};

struct vmm_guest_request {
	struct dlist head;
	void *data;
	void (*func)(struct vmm_guest *, void *);
};

struct vmm_vcpu_irq {
	atomic_t assert;
	u64 reason;
};

struct vmm_vcpu_irqs {
	u32 irq_count;
	struct vmm_vcpu_irq *irq;
	atomic_t execute_pending;
	atomic64_t assert_count;
	atomic64_t execute_count;
	atomic64_t clear_count;
	atomic64_t deassert_count;
	struct {
		vmm_spinlock_t lock;
		u32 yield_count;
		bool state;
		void *priv;
	} wfi;
};

struct vmm_guest {
	struct dlist head;

	/* General information */
	u32 id;
	char name[VMM_FIELD_NAME_SIZE];
	struct vmm_devtree_node *node;
	bool is_big_endian;
	u32 reset_count;
	u64 reset_tstamp;

	/* Request queue */
	vmm_spinlock_t req_lock;
	struct dlist req_list;

	/* VCPU instances belonging to this Guest */
	vmm_rwlock_t vcpu_lock;
	u32 vcpu_count;
	struct dlist vcpu_list;

	/* Guest address space */
	struct vmm_guest_aspace aspace;

	/* Architecture specific context */
	void *arch_priv;
};

enum vmm_vcpu_states {
	VMM_VCPU_STATE_UNKNOWN = 0x01,
	VMM_VCPU_STATE_RESET = 0x02,
	VMM_VCPU_STATE_READY = 0x04,
	VMM_VCPU_STATE_RUNNING = 0x08,
	VMM_VCPU_STATE_PAUSED = 0x10,
	VMM_VCPU_STATE_HALTED = 0x20
};

#define VMM_VCPU_STATE_ALLMASK		0xff

#define VMM_VCPU_STATE_SAVEABLE		( VMM_VCPU_STATE_RUNNING | \
					  VMM_VCPU_STATE_PAUSED | \
					  VMM_VCPU_STATE_HALTED )

#define VMM_VCPU_STATE_INTERRUPTIBLE	( VMM_VCPU_STATE_RUNNING | \
					  VMM_VCPU_STATE_READY | \
					  VMM_VCPU_STATE_PAUSED )

#define VMM_VCPU_MIN_PRIORITY		0
#define VMM_VCPU_MAX_PRIORITY		7
#define VMM_VCPU_DEF_PRIORITY		3
#define VMM_VCPU_DEF_TIME_SLICE		(CONFIG_TSLICE_MS * 1000000)
#define VMM_VCPU_DEF_DEADLINE		(VMM_VCPU_DEF_TIME_SLICE * 10)
#define VMM_VCPU_DEF_PERIODICITY	(VMM_VCPU_DEF_DEADLINE * 10)

struct vmm_vcpu_resource {
	struct dlist head;
	const char *name;
	void (*cleanup)(struct vmm_vcpu *vcpu, struct vmm_vcpu_resource *res);
};

struct vmm_vcpu {
	struct dlist head;

	/* General information */
	u32 id;
	u32 subid;
	char name[VMM_FIELD_NAME_SIZE];
	struct vmm_devtree_node *node;
	bool is_normal;
	bool is_poweroff;
	struct vmm_guest *guest;

	/* Start PC and stack */
	virtual_addr_t start_pc;
	virtual_addr_t stack_va;
	virtual_size_t stack_sz;

	/* Scheduler dynamic context */
	vmm_rwlock_t sched_lock;
	u32 hcpu;
	const struct vmm_cpumask *cpu_affinity;
	atomic_t state;
	u64 state_tstamp;
	u64 state_ready_nsecs;
	u64 state_running_nsecs;
	u64 state_paused_nsecs;
	u64 state_halted_nsecs;
	u64 system_nsecs;
	u32 reset_count;
	u64 reset_tstamp;
	u32 preempt_count;
	bool resumed;
	void *sched_priv;

	/* Scheduler static context */
	u8 priority;
	u64 time_slice;
	u64 deadline;
	u64 periodicity;

	/* Architecture specific context */
	arch_regs_t regs;
	void *arch_priv;

	/* Virtual IRQ context */
	struct vmm_vcpu_irqs irqs;

	/* Resources acquired */
	vmm_spinlock_t res_lock;
	struct dlist res_head;

	/* Waitqueue context */
	struct dlist wq_head;
	vmm_spinlock_t *wq_lock;
	void *wq_priv;

	/* Waitqueue cleanup callback */
	void (*wq_cleanup)(struct vmm_vcpu *);
};

/** Acquire manager lock */
void vmm_manager_lock(void);

/** Release manager lock */
void vmm_manager_unlock(void);

/** Maximum number of VCPUs */
u32 vmm_manager_max_vcpu_count(void);

/** Current number of VCPUs (orphan + normal) */
u32 vmm_manager_vcpu_count(void);

/** Retrieve VCPU with given ID.
 *  Returns NULL if there is no VCPU associated with given ID.
 */
struct vmm_vcpu *vmm_manager_vcpu(u32 vcpu_id);

/** Iterate over each VCPU with manager lock held */
int vmm_manager_vcpu_iterate(int (*iter)(struct vmm_vcpu *, void *),
			     void *priv);

/** Retriver VCPU state */
u32 vmm_manager_vcpu_get_state(struct vmm_vcpu *vcpu);

/** Update VCPU state
 *  Note: Avoid calling this function directly
 */
int vmm_manager_vcpu_set_state(struct vmm_vcpu *vcpu, u32 state);

/** Reset a VCPU */
#define vmm_manager_vcpu_reset(vcpu)	\
		vmm_manager_vcpu_set_state((vcpu), VMM_VCPU_STATE_RESET)

/** Kick a VCPU out of reset state */
#define vmm_manager_vcpu_kick(vcpu)	\
		vmm_manager_vcpu_set_state((vcpu), VMM_VCPU_STATE_READY)

/** Pause a VCPU */
#define vmm_manager_vcpu_pause(vcpu)	\
		vmm_manager_vcpu_set_state((vcpu), VMM_VCPU_STATE_PAUSED)

/** Resume a VCPU */
#define vmm_manager_vcpu_resume(vcpu)	\
		vmm_manager_vcpu_set_state((vcpu), VMM_VCPU_STATE_READY)

/** Halt a VCPU */
#define vmm_manager_vcpu_halt(vcpu)	\
		vmm_manager_vcpu_set_state((vcpu), VMM_VCPU_STATE_HALTED)

/** Retrive host CPU assigned to given VCPU */
int vmm_manager_vcpu_get_hcpu(struct vmm_vcpu *vcpu, u32 *hcpu);

/** Check host CPU assigned to given VCPU is current host CPU */
bool vmm_manager_vcpu_check_current_hcpu(struct vmm_vcpu *vcpu);

/** Update host CPU assigned to given VCPU */
int vmm_manager_vcpu_set_hcpu(struct vmm_vcpu *vcpu, u32 hcpu);

/** Force re-scheduling on host CPU assigned to given VCPU */
int vmm_manager_vcpu_hcpu_resched(struct vmm_vcpu *vcpu);

/** Call function on host CPU assigned to given VCPU if
 *  VCPU state matches given state_mask
 */
int vmm_manager_vcpu_hcpu_func(struct vmm_vcpu *vcpu,
			       u32 state_mask,
			       void (*func)(struct vmm_vcpu *, void *),
			       void *data, bool use_async);

/** Retrive host CPU affinity of given VCPU */
const struct vmm_cpumask *vmm_manager_vcpu_get_affinity(struct vmm_vcpu *vcpu);

/** Update host CPU affinity of given VCPU */
int vmm_manager_vcpu_set_affinity(struct vmm_vcpu *vcpu,
				  const struct vmm_cpumask *cpu_mask);

/** Add resource to VCPU resource list */
int vmm_manager_vcpu_resource_add(struct vmm_vcpu *vcpu,
				  struct vmm_vcpu_resource *res);

/** Remove resource from VCPU resource list */
int vmm_manager_vcpu_resource_remove(struct vmm_vcpu *vcpu,
				     struct vmm_vcpu_resource *res);

/** Create an orphan VCPU */
struct vmm_vcpu *vmm_manager_vcpu_orphan_create(const char *name,
					virtual_addr_t start_pc,
					virtual_size_t stack_sz,
					u8 priority,
					u64 time_slice_nsecs,
					u64 deadline,
					u64 periodicity,
					const struct vmm_cpumask *affinity);

/** Destroy an orphan VCPU */
int vmm_manager_vcpu_orphan_destroy(struct vmm_vcpu *vcpu);

/** Maximum number of Guests */
u32 vmm_manager_max_guest_count(void);

/** Current number of Guests */
u32 vmm_manager_guest_count(void);

/** Retrieve Guest with given ID.
 *  Returns NULL if there is no Guest associated with given ID.
 */
struct vmm_guest *vmm_manager_guest(u32 guest_id);

/** Find Guest with given name.
 *  Returns NULL if there is no Guest with given name.
 */
struct vmm_guest *vmm_manager_guest_find(const char *guest_name);

/** Iterate over each Guest with manager lock held */
int vmm_manager_guest_iterate(int (*iter)(struct vmm_guest *, void *),
			      void *priv);

/** Number of VCPUs belonging to a given Guest */
u32 vmm_manager_guest_vcpu_count(struct vmm_guest *guest);

/** Retrieve VCPU belonging to a given Guest with particular subid */
struct vmm_vcpu *vmm_manager_guest_vcpu(struct vmm_guest *guest, u32 subid);

/** Get next VCPU of a given Guest */
struct vmm_vcpu *vmm_manager_guest_next_vcpu(const struct vmm_guest *guest,
					     struct vmm_vcpu *current);

/** Iterate over each VCPU of a given Guest */
#define vmm_manager_for_each_guest_vcpu(__v, __g)	\
		for (__v = vmm_manager_guest_next_vcpu(__g, NULL); __v; \
		     __v = vmm_manager_guest_next_vcpu(__g, __v))

/** Iterate over each VCPU of a given Guest with guest->vcpu_lock held */
int vmm_manager_guest_vcpu_iterate(struct vmm_guest *guest,
				   int (*iter)(struct vmm_vcpu *, void *),
				   void *priv);

/** Reset a Guest */
int vmm_manager_guest_reset(struct vmm_guest *guest);

/** Last Reset timestamp of a Guest */
u64 vmm_manager_guest_reset_timestamp(struct vmm_guest *guest);

/** Kick a Guest out of reset state */
int vmm_manager_guest_kick(struct vmm_guest *guest);

/** Pause a Guest */
int vmm_manager_guest_pause(struct vmm_guest *guest);

/** Resume a Guest */
int vmm_manager_guest_resume(struct vmm_guest *guest);

/** Halt a Guest */
int vmm_manager_guest_halt(struct vmm_guest *guest);

/** Schedule request callback for a Guest
 *  NOTE: Use this only for non-performance critical function
 *  because we use one vmm_work per-Guest to process all Guest
 *  request. For performance critical functions use different
 *  bottom-half mechanism for processing Guest request.
 *  NOTE: If Guest is destroyed then all its pending requests
 *  will be ignored.
 */
int vmm_manager_guest_request(struct vmm_guest *guest,
			      void (*req_func)(struct vmm_guest *, void *),
			      void *req_data);

/** Schedule reboot request for a Guest
 *  NOTE: This request will first reset the Guest then kick it.
 */
int vmm_manager_guest_reboot_request(struct vmm_guest *guest);

/** Schedule shutdown request for a Guest
 *  NOTE: This request will only reset the Guest.
 */
int vmm_manager_guest_shutdown_request(struct vmm_guest *guest);

/** Create a Guest based on device tree configuration */
struct vmm_guest *vmm_manager_guest_create(struct vmm_devtree_node *gnode);

/** Destroy a Guest */
int vmm_manager_guest_destroy(struct vmm_guest *guest);

/** Initialize manager */
int vmm_manager_init(void);

#endif
