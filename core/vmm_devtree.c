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
 * @file vmm_devtree.c
 * @author Anup Patel (anup@brainfault.org)
 * @brief Device Tree Implementation.
 */

#include <vmm_error.h>
#include <vmm_compiler.h>
#include <vmm_heap.h>
#include <vmm_stdio.h>
#include <vmm_host_io.h>
#include <vmm_devtree.h>
#include <arch_sections.h>
#include <arch_devtree.h>
#include <libs/mathlib.h>
#include <libs/stringlib.h>

struct vmm_devtree_ctrl {
        struct vmm_devtree_node *root;
	u32 nidtbl_count;
	struct vmm_devtree_nidtbl_entry *nidtbl;
};

static struct vmm_devtree_ctrl dtree_ctrl;

bool vmm_devtree_isliteral(u32 attrtype)
{
	bool ret = FALSE;

	switch(attrtype) {
	case VMM_DEVTREE_ATTRTYPE_UINT32:
	case VMM_DEVTREE_ATTRTYPE_UINT64:
	case VMM_DEVTREE_ATTRTYPE_VIRTADDR:
	case VMM_DEVTREE_ATTRTYPE_VIRTSIZE:
	case VMM_DEVTREE_ATTRTYPE_PHYSADDR:
	case VMM_DEVTREE_ATTRTYPE_PHYSSIZE:
		ret = TRUE;
		break;
	};

	return ret;
}

u32 vmm_devtree_literal_size(u32 attrtype)
{
	u32 ret = 0;

	switch(attrtype) {
	case VMM_DEVTREE_ATTRTYPE_UINT32:
		ret = sizeof(u32);
		break;
	case VMM_DEVTREE_ATTRTYPE_UINT64:
		ret = sizeof(u64);
		break;
	case VMM_DEVTREE_ATTRTYPE_VIRTADDR:
		ret = sizeof(virtual_addr_t);
		break;
	case VMM_DEVTREE_ATTRTYPE_VIRTSIZE:
		ret = sizeof(virtual_size_t);
		break;
	case VMM_DEVTREE_ATTRTYPE_PHYSADDR:
		ret = sizeof(physical_addr_t);
		break;
	case VMM_DEVTREE_ATTRTYPE_PHYSSIZE:
		ret = sizeof(physical_size_t);
		break;
	};

	return ret;
}

u32 vmm_devtree_estimate_attrtype(const char *name)
{
	u32 ret = VMM_DEVTREE_ATTRTYPE_BYTEARRAY;

	if (!name) {
		return ret;
	}

	if (!strcmp(name, VMM_DEVTREE_MODEL_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_STRING;
	} else if (!strcmp(name, VMM_DEVTREE_DEVICE_TYPE_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_STRING;
	} else if (!strcmp(name, VMM_DEVTREE_COMPATIBLE_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_STRING;
	} else if (!strcmp(name, VMM_DEVTREE_CLOCK_FREQ_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_UINT32;
	} else if (!strcmp(name, VMM_DEVTREE_CLOCKS_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_UINT32;
	} else if (!strcmp(name, VMM_DEVTREE_CLOCK_NAMES_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_STRING;
	} else if (!strcmp(name, VMM_DEVTREE_CLOCK_OUT_NAMES_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_STRING;
	} else if (!strcmp(name, VMM_DEVTREE_REG_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_UINT32;
	} else if (!strcmp(name, VMM_DEVTREE_VIRTUAL_REG_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_VIRTADDR;
	} else if (!strcmp(name, VMM_DEVTREE_RANGES_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_UINT32;
	} else if (!strcmp(name, VMM_DEVTREE_ADDR_CELLS_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_UINT32;
	} else if (!strcmp(name, VMM_DEVTREE_SIZE_CELLS_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_UINT32;
	} else if (!strcmp(name, VMM_DEVTREE_PHANDLE_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_UINT32;
	} else if (!strcmp(name, VMM_DEVTREE_MEMORY_PHYS_ADDR_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_PHYSADDR;
	} else if (!strcmp(name, VMM_DEVTREE_MEMORY_PHYS_SIZE_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_PHYSSIZE;
	} else if (!strcmp(name, VMM_DEVTREE_ENABLE_METHOD_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_STRING;
	} else if (!strcmp(name, VMM_DEVTREE_CPU_RELEASE_ADDR_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_PHYSADDR;
	} else if (!strcmp(name, VMM_DEVTREE_CPU_CLEAR_ADDR_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_PHYSADDR;
	} else if (!strcmp(name, VMM_DEVTREE_INTERRUPTS_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_UINT32;
	} else if (!strcmp(name, VMM_DEVTREE_ENDIANNESS_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_STRING;
	} else if (!strcmp(name, VMM_DEVTREE_START_PC_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_VIRTADDR;
	} else if (!strcmp(name, VMM_DEVTREE_PRIORITY_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_UINT32;
	} else if (!strcmp(name, VMM_DEVTREE_TIME_SLICE_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_UINT64;
	} else if (!strcmp(name, VMM_DEVTREE_MANIFEST_TYPE_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_STRING;
	} else if (!strcmp(name, VMM_DEVTREE_ADDRESS_TYPE_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_STRING;
	} else if (!strcmp(name, VMM_DEVTREE_GUEST_PHYS_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_PHYSADDR;
	} else if (!strcmp(name, VMM_DEVTREE_HOST_PHYS_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_PHYSADDR;
	} else if (!strcmp(name, VMM_DEVTREE_ALIAS_PHYS_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_PHYSADDR;
	} else if (!strcmp(name, VMM_DEVTREE_PHYS_SIZE_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_PHYSSIZE;
	} else if (!strcmp(name, VMM_DEVTREE_ALIGN_ORDER_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_UINT32;
	} else if (!strcmp(name, VMM_DEVTREE_SWITCH_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_STRING;
	} else if (!strcmp(name, VMM_DEVTREE_CONSOLE_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_STRING;
	} else if (!strcmp(name, VMM_DEVTREE_RTCDEV_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_STRING;
	} else if (!strcmp(name, VMM_DEVTREE_BOOTARGS_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_STRING;
	} else if (!strcmp(name, VMM_DEVTREE_BOOTCMD_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_STRING;
	} else if (!strcmp(name, VMM_DEVTREE_BLKDEV_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_STRING;
	} else if (!strcmp(name, VMM_DEVTREE_VCPU_AFFINITY_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_UINT32;
	} else if (!strcmp(name, VMM_DEVTREE_VCPU_POWEROFF_ATTR_NAME)) {
		ret = VMM_DEVTREE_ATTRTYPE_UINT32;
	}

	return ret;
}

static int devtree_node_is_compatible(const struct vmm_devtree_node *node,
				      const char *compat)
{
	const char *cp;
	int cplen, l;

	cp = vmm_devtree_attrval(node, VMM_DEVTREE_COMPATIBLE_ATTR_NAME);
	cplen = vmm_devtree_attrlen(node, VMM_DEVTREE_COMPATIBLE_ATTR_NAME);
	if (cp == NULL)
		return 0;
	while (cplen > 0) {
		if (strcmp(cp, compat) == 0)
			return 1;
		l = strlen(cp) + 1;
		cp += l;
		cplen -= l;
	}

	return 0;
}

const void *vmm_devtree_attrval(const struct vmm_devtree_node *node,
				const char *attrib)
{
	struct vmm_devtree_attr *attr;

	if (!node || !attrib) {
		return NULL;
	}

	vmm_devtree_for_each_attr(attr, node) {
		if (strcmp(attr->name, attrib) == 0) {
			return attr->value;
		}
	}

	return NULL;
}

u32 vmm_devtree_attrlen(const struct vmm_devtree_node *node,
			const char *attrib)
{
	struct vmm_devtree_attr *attr;

	if (!node || !attrib) {
		return 0;
	}

	vmm_devtree_for_each_attr(attr, node) {
		if (strcmp(attr->name, attrib) == 0) {
			return attr->len;
		}
	}

	return 0;
}

bool vmm_devtree_have_attr(const struct vmm_devtree_node *node)
{
	bool ret;
	irq_flags_t flags;
	struct vmm_devtree_node *np = (struct vmm_devtree_node *)node;

	if (!np) {
		return FALSE;
	}

	vmm_read_lock_irqsave_lite(&np->attr_lock, flags);
	ret = list_empty(&np->attr_list) ? FALSE : TRUE;
	vmm_read_unlock_irqrestore_lite(&np->attr_lock, flags);

	return ret;
}

struct vmm_devtree_attr *vmm_devtree_next_attr(
					const struct vmm_devtree_node *node,
					struct vmm_devtree_attr *current)
{
	irq_flags_t flags;
	struct vmm_devtree_attr *ret = NULL;
	struct vmm_devtree_node *np = (struct vmm_devtree_node *)node;

	if (!np) {
		return NULL;
	}

	vmm_read_lock_irqsave_lite(&np->attr_lock, flags);
	if (!current) {
		if (!list_empty(&np->attr_list)) {
			ret = list_first_entry(&np->attr_list,
						struct vmm_devtree_attr,
						head);
		}
	} else if (!list_is_last(&current->head, &np->attr_list)) {
		ret = list_first_entry(&current->head,
					struct vmm_devtree_attr,
					head);
	}
	vmm_read_unlock_irqrestore_lite(&np->attr_lock, flags);

	return ret;
}

int vmm_devtree_setattr(struct vmm_devtree_node *node,
			const char *name, void *value,
			u32 type, u32 len, bool value_is_be)
{
	u32 i, sz, cnt;
	bool found;
	irq_flags_t flags;
	struct vmm_devtree_attr *attr;

	if (!node || !name ||
	    (len && !value) ||
	    (VMM_DEVTREE_MAX_ATTRTYPE <= type)) {
		return VMM_EINVALID;
	}

	found = FALSE;
	vmm_devtree_for_each_attr(attr, node) {
		if (strcmp(attr->name, name) == 0) {
			found = TRUE;
			break;
		}
	}

	if (!found) {
		attr = vmm_malloc(sizeof(struct vmm_devtree_attr));
		if (!attr) {
			return VMM_ENOMEM;
		}
		INIT_LIST_HEAD(&attr->head);
		attr->len = len;
		attr->type = type;
		strncpy(attr->name, name, sizeof(attr->name));
		if (attr->len) {
			attr->value = vmm_malloc(attr->len);
			if (!attr->value) {
				vmm_free(attr);
				return VMM_ENOMEM;
			}
			memcpy(attr->value, value, attr->len);
		} else {
			attr->value = NULL;
		}
		vmm_write_lock_irqsave_lite(&node->attr_lock, flags);
		list_add_tail(&attr->head, &node->attr_list);
		vmm_write_unlock_irqrestore_lite(&node->attr_lock, flags);
	} else {
		attr->type = type;
		if (attr->len != len) {
			if (attr->len) {
				vmm_free(attr->value);
				attr->value = NULL;
				attr->len = 0;
			}
			attr->len = len;
			if (attr->len) {
				attr->value = vmm_malloc(attr->len);
				if (!attr->value) {
					attr->len = 0;
					return VMM_ENOMEM;
				}
			} else {
				attr->value = NULL;
			}
		}
		if (attr->len) {
			memcpy(attr->value, value, attr->len);
		}
	}

	if (attr->value && !value_is_be) {
		cnt = 0;
		sz = 0;
		switch (type) {
		case VMM_DEVTREE_ATTRTYPE_UINT32:
			sz = sizeof(u32);
			cnt = udiv32(len, sz);
			break;
		case VMM_DEVTREE_ATTRTYPE_UINT64:
			sz = sizeof(u64);
			cnt = udiv32(len, sz);
			break;
		case VMM_DEVTREE_ATTRTYPE_VIRTADDR:
			if (sizeof(virtual_addr_t) == sizeof(u64)) {
				sz = sizeof(u64);
			} else {
			}
			cnt = udiv32(len, sz);
			break;
		case VMM_DEVTREE_ATTRTYPE_VIRTSIZE:
			if (sizeof(virtual_size_t) == sizeof(u64)) {
				sz = sizeof(u64);
			} else {
				sz = sizeof(u32);
			}
			cnt = udiv32(len, sz);
			break;
		case VMM_DEVTREE_ATTRTYPE_PHYSADDR:
			if (sizeof(physical_addr_t) == sizeof(u64)) {
				sz = sizeof(u64);
			} else {
				sz = sizeof(u32);
			}
			cnt = udiv32(len, sz);
			break;
		case VMM_DEVTREE_ATTRTYPE_PHYSSIZE:
			if (sizeof(physical_size_t) == sizeof(u64)) {
				sz = sizeof(u64);
			} else {
				sz = sizeof(u32);
			}
			cnt = udiv32(len, sz);
			break;
		default:
			break;
		};

		for (i = 0; i < cnt; i++) {
			switch (sz) {
			case 4:
				((u32 *)attr->value)[i] =
					vmm_cpu_to_be32(((u32 *)attr->value)[i]);
				break;
			case 8:
				((u64 *)attr->value)[i] =
					vmm_cpu_to_be64(((u64 *)attr->value)[i]);
				break;
			default:
				break;
			};
		}
	}

	return VMM_OK;
}

struct vmm_devtree_attr *vmm_devtree_getattr(
					const struct vmm_devtree_node *node,
					const char *name)
{
	struct vmm_devtree_attr *attr;

	if (!node || !name) {
		return NULL;
	}

	vmm_devtree_for_each_attr(attr, node) {
		if (strcmp(attr->name, name) == 0) {
			return attr;
		}
	}

	return NULL;
}

int vmm_devtree_delattr(struct vmm_devtree_node *node, const char *name)
{
	irq_flags_t flags;
	struct vmm_devtree_attr *attr;

	if (!node || !name) {
		return VMM_EFAIL;
	}

	attr = vmm_devtree_getattr(node, name);
	if (!attr) {
		return VMM_EFAIL;
	}

	if (attr->value) {
		vmm_free(attr->value);
	}

	vmm_write_lock_irqsave_lite(&node->attr_lock, flags);
	list_del(&attr->head);
	vmm_write_unlock_irqrestore_lite(&node->attr_lock, flags);

	vmm_free(attr);

	return VMM_OK;
}

int vmm_devtree_read_u8_atindex(const struct vmm_devtree_node *node,
			        const char *attrib, u8 *out, int index)
{
	u32 asz;
	const u8 *aval;
	struct vmm_devtree_attr *attr;

	if (!node || !attrib || !out || (index<0)) {
		return VMM_EINVALID;
	}

	attr = vmm_devtree_getattr(node, attrib);
	if (!attr) {
		return VMM_EINVALID;
	}

	aval = attr->value;
	if (!aval) {
		return VMM_ENOTAVAIL;
	}

	asz = vmm_devtree_attrlen(node, attrib);
	if (asz <= index) {
		return VMM_ENOTAVAIL;
	}

	*out = aval[index];

	return VMM_OK;
}

int vmm_devtree_read_u8_array(const struct vmm_devtree_node *node,
			      const char *attrib, u8 *out, size_t sz)
{
	u32 i, asz;
	const u8 *aval;
	struct vmm_devtree_attr *attr;

	if (!node || !attrib || !out || !sz) {
		return VMM_EINVALID;
	}

	attr = vmm_devtree_getattr(node, attrib);
	if (!attr) {
		return VMM_EINVALID;
	}

	aval = attr->value;
	if (!aval) {
		return VMM_ENOTAVAIL;
	}

	asz = vmm_devtree_attrlen(node, attrib);
	if (asz < sz) {
		return VMM_ENOTAVAIL;
	}

	for (i = 0; i < asz; i++) {
		out[i] = aval[i];
	}

	return VMM_OK;
}

int vmm_devtree_read_u16_atindex(const struct vmm_devtree_node *node,
			         const char *attrib, u16 *out, int index)
{
	bool found;
	u32 i, s, asz;
	const void *aval;
	struct vmm_devtree_attr *attr;

	if (!node || !attrib || !out || (index<0)) {
		return VMM_EINVALID;
	}

	attr = vmm_devtree_getattr(node, attrib);
	if (!attr) {
		return VMM_EINVALID;
	}

	aval = attr->value;
	if (!aval) {
		return VMM_ENOTAVAIL;
	}

	i = 0;
	found = FALSE;
	asz = vmm_devtree_attrlen(node, attrib);
	while (asz) {
		s = (asz < sizeof(u16)) ? asz : sizeof(u16);

		if (i == index) {
			switch (s) {
			case 1:
				*out = *((const u8 *)aval);
				break;
			case 2:
				*out = vmm_be16_to_cpu(*((const u16 *)aval));
				break;
			default:
				return VMM_EFAIL;
			};
			found = TRUE;
			break;
		}

		aval += s;
		asz -= s;
		i++;
	}

	if (!found) {
		return VMM_ENOTAVAIL;
	}

	return VMM_OK;
}

int vmm_devtree_read_u16_array(const struct vmm_devtree_node *node,
			       const char *attrib, u16 *out, size_t sz)
{
	u32 i, s, asz;
	const void *aval;
	struct vmm_devtree_attr *attr;

	if (!node || !attrib || !out || (sz<=0)) {
		return VMM_EINVALID;
	}

	attr = vmm_devtree_getattr(node, attrib);
	if (!attr) {
		return VMM_EINVALID;
	}

	aval = attr->value;
	if (!aval) {
		return VMM_ENOTAVAIL;
	}

	i = 0;
	asz = vmm_devtree_attrlen(node, attrib);
	while (asz && (i < sz)) {
		s = (asz < sizeof(u16)) ? asz : sizeof(u16);

		switch (s) {
		case 1:
			out[i] = *((const u8 *)aval);
			break;
		case 2:
			out[i] = vmm_be16_to_cpu(*((const u16 *)aval));
			break;
		default:
			return VMM_EFAIL;
		};

		aval += s;
		asz -= s;
		i++;
	}

	if (i < sz) {
		return VMM_ENOTAVAIL;
	}

	return VMM_OK;
}

int vmm_devtree_read_u32_atindex(const struct vmm_devtree_node *node,
			         const char *attrib, u32 *out, int index)
{
	bool found;
	u32 i, s, asz;
	const void *aval;
	struct vmm_devtree_attr *attr;

	if (!node || !attrib || !out || (index<0)) {
		return VMM_EINVALID;
	}

	attr = vmm_devtree_getattr(node, attrib);
	if (!attr) {
		return VMM_EINVALID;
	}

	aval = attr->value;
	if (!aval) {
		return VMM_ENOTAVAIL;
	}

	i = 0;
	found = FALSE;
	asz = vmm_devtree_attrlen(node, attrib);
	while (asz) {
		s = (asz < sizeof(u32)) ? asz : sizeof(u32);

		if (i == index) {
			switch (s) {
			case 1:
				*out = *((const u8 *)aval);
				break;
			case 2:
				*out = vmm_be16_to_cpu(*((const u16 *)aval));
				break;
			case 4:
				*out = vmm_be32_to_cpu(*((const u32 *)aval));
				break;
			default:
				return VMM_EFAIL;
			};
			found = TRUE;
			break;
		}

		aval += s;
		asz -= s;
		i++;
	}

	if (!found) {
		return VMM_ENOTAVAIL;
	}

	return VMM_OK;
}

int vmm_devtree_read_u32_array(const struct vmm_devtree_node *node,
			       const char *attrib, u32 *out, size_t sz)
{
	u32 i, s, asz;
	const void *aval;
	struct vmm_devtree_attr *attr;

	if (!node || !attrib || !out || (sz<=0)) {
		return VMM_EINVALID;
	}

	attr = vmm_devtree_getattr(node, attrib);
	if (!attr) {
		return VMM_EINVALID;
	}

	aval = attr->value;
	if (!aval) {
		return VMM_ENOTAVAIL;
	}

	i = 0;
	asz = vmm_devtree_attrlen(node, attrib);
	while (asz && (i < sz)) {
		s = (asz < sizeof(u32)) ? asz : sizeof(u32);

		switch (s) {
		case 1:
			out[i] = *((const u8 *)aval);
			break;
		case 2:
			out[i] = vmm_be16_to_cpu(*((const u16 *)aval));
			break;
		case 4:
			out[i] = vmm_be32_to_cpu(*((const u32 *)aval));
			break;
		default:
			return VMM_EFAIL;
		};

		aval += s;
		asz -= s;
		i++;
	}

	if (i < sz) {
		return VMM_ENOTAVAIL;
	}

	return VMM_OK;
}

int vmm_devtree_read_u64_atindex(const struct vmm_devtree_node *node,
			         const char *attrib, u64 *out, int index)
{
	bool found;
	u32 i, s, asz;
	const void *aval;
	struct vmm_devtree_attr *attr;

	if (!node || !attrib || !out || (index<0)) {
		return VMM_EINVALID;
	}

	attr = vmm_devtree_getattr(node, attrib);
	if (!attr) {
		return VMM_EINVALID;
	}

	aval = attr->value;
	if (!aval) {
		return VMM_ENOTAVAIL;
	}

	i = 0;
	found = FALSE;
	asz = vmm_devtree_attrlen(node, attrib);
	while (asz) {
		s = (asz < sizeof(u64)) ? asz : sizeof(u64);

		if (i == index) {
			switch (s) {
			case 1:
				*out = *((const u8 *)aval);
				break;
			case 2:
				*out = vmm_be16_to_cpu(*((const u16 *)aval));
				break;
			case 4:
				*out = vmm_be32_to_cpu(*((const u32 *)aval));
				break;
			case 8:
				*out = vmm_be64_to_cpu(*((const u64 *)aval));
				break;
			default:
				return VMM_EFAIL;
			};
			found = TRUE;
			break;
		}

		aval += s;
		asz -= s;
		i++;
	}

	if (!found) {
		return VMM_ENOTAVAIL;
	}

	return VMM_OK;
}

int vmm_devtree_read_u64_array(const struct vmm_devtree_node *node,
			       const char *attrib, u64 *out, size_t sz)
{
	u32 i, s, asz;
	const void *aval;
	struct vmm_devtree_attr *attr;

	if (!node || !attrib || !out || (sz<=0)) {
		return VMM_EINVALID;
	}

	attr = vmm_devtree_getattr(node, attrib);
	if (!attr) {
		return VMM_EINVALID;
	}

	aval = attr->value;
	if (!aval) {
		return VMM_ENOTAVAIL;
	}

	i = 0;
	asz = vmm_devtree_attrlen(node, attrib);
	while (asz && (i < sz)) {
		s = (asz < sizeof(u64)) ? asz : sizeof(u64);

		switch (s) {
		case 1:
			out[i] = *((const u8 *)aval);
			break;
		case 2:
			out[i] = vmm_be16_to_cpu(*((const u16 *)aval));
			break;
		case 4:
			out[i] = vmm_be32_to_cpu(*((const u32 *)aval));
			break;
		case 8:
			out[i] = vmm_be64_to_cpu(*((const u64 *)aval));
			break;
		default:
			break;
		};

		aval += s;
		asz -= s;
		i++;
	}

	if (i < sz) {
		return VMM_ENOTAVAIL;
	}

	return VMM_OK;
}

int vmm_devtree_read_physaddr_atindex(const struct vmm_devtree_node *node,
				      const char *attrib, physical_addr_t *out,
				      int index)
{
	if (sizeof(physical_addr_t) == sizeof(u32)) {
		return vmm_devtree_read_u32_atindex(node, attrib,
						    (u32 *)out, index);
	} else if (sizeof(physical_addr_t) == sizeof(u64)) {
		return vmm_devtree_read_u64_atindex(node, attrib,
						    (u64 *)out, index);
	}

	return VMM_EFAIL;
}

int vmm_devtree_read_physaddr_array(const struct vmm_devtree_node *node,
				    const char *attrib, physical_addr_t *out,
				    size_t sz)
{
	if (sizeof(physical_addr_t) == sizeof(u32)) {
		return vmm_devtree_read_u32_array(node, attrib,
						  (u32 *)out, sz);
	} else if (sizeof(physical_addr_t) == sizeof(u64)) {
		return vmm_devtree_read_u64_array(node, attrib,
						  (u64 *)out, sz);
	}

	return VMM_EFAIL;
}

int vmm_devtree_read_physsize_atindex(const struct vmm_devtree_node *node,
				      const char *attrib, physical_size_t *out,
				      int index)
{
	if (sizeof(physical_size_t) == sizeof(u32)) {
		return vmm_devtree_read_u32_atindex(node, attrib,
						    (u32 *)out, index);
	} else if (sizeof(physical_size_t) == sizeof(u64)) {
		return vmm_devtree_read_u64_atindex(node, attrib,
						    (u64 *)out, index);
	}

	return VMM_EFAIL;
}

int vmm_devtree_read_physsize_array(const struct vmm_devtree_node *node,
				    const char *attrib, physical_size_t *out,
				    size_t sz)
{
	if (sizeof(physical_size_t) == sizeof(u32)) {
		return vmm_devtree_read_u32_array(node, attrib,
						  (u32 *)out, sz);
	} else if (sizeof(physical_size_t) == sizeof(u64)) {
		return vmm_devtree_read_u64_array(node, attrib,
						  (u64 *)out, sz);
	}

	return VMM_EFAIL;
}

int vmm_devtree_read_virtaddr_atindex(const struct vmm_devtree_node *node,
				      const char *attrib, virtual_addr_t *out,
				      int index)
{
	if (sizeof(virtual_addr_t) == sizeof(u32)) {
		return vmm_devtree_read_u32_atindex(node, attrib,
						    (u32 *)out, index);
	} else if (sizeof(virtual_addr_t) == sizeof(u64)) {
		return vmm_devtree_read_u64_atindex(node, attrib,
						    (u64 *)out, index);
	}

	return VMM_EFAIL;
}

int vmm_devtree_read_virtaddr_array(const struct vmm_devtree_node *node,
				    const char *attrib, virtual_addr_t *out,
				    size_t sz)
{
	if (sizeof(virtual_addr_t) == sizeof(u32)) {
		return vmm_devtree_read_u32_array(node, attrib,
						  (u32 *)out, sz);
	} else if (sizeof(virtual_addr_t) == sizeof(u64)) {
		return vmm_devtree_read_u64_array(node, attrib,
						  (u64 *)out, sz);
	}

	return VMM_EFAIL;
}

int vmm_devtree_read_virtsize_atindex(const struct vmm_devtree_node *node,
				      const char *attrib, virtual_size_t *out,
				      int index)
{
	if (sizeof(virtual_size_t) == sizeof(u32)) {
		return vmm_devtree_read_u32_atindex(node, attrib,
						    (u32 *)out, index);
	} else if (sizeof(virtual_size_t) == sizeof(u64)) {
		return vmm_devtree_read_u64_atindex(node, attrib,
						    (u64 *)out, index);
	}

	return VMM_EFAIL;
}

int vmm_devtree_read_virtsize_array(const struct vmm_devtree_node *node,
				    const char *attrib, virtual_size_t *out,
				    size_t sz)
{
	if (sizeof(virtual_size_t) == sizeof(u32)) {
		return vmm_devtree_read_u32_array(node, attrib,
						  (u32 *)out, sz);
	} else if (sizeof(virtual_size_t) == sizeof(u64)) {
		return vmm_devtree_read_u64_array(node, attrib,
						  (u64 *)out, sz);
	}

	return VMM_EFAIL;
}

int vmm_devtree_read_string(const struct vmm_devtree_node *node,
			    const char *attrib, const char **out)
{
	const char *aval;

	if (!node || !attrib || !out) {
		return VMM_EINVALID;
	}

	aval = vmm_devtree_attrval(node, attrib);
	if (!aval) {
		return VMM_ENOTAVAIL;
	}

	*out = aval;

	return VMM_OK;
}

int vmm_devtree_match_string(struct vmm_devtree_node *node,
			     const char *attrib, const char *string)
{
	int i;
	size_t l;
	const char *p, *end;
	struct vmm_devtree_attr *attr = vmm_devtree_getattr(node, attrib);

	if (!attr)
		return VMM_EINVALID;
	if (!attr->value)
		return VMM_ENODATA;

	p = attr->value;
	end = p + attr->len;

	for (i = 0; p < end; i++, p += l) {
		l = strlen(p) + 1;
		if (p + l > end)
			return VMM_EILSEQ;
		if (strcmp(string, p) == 0)
			return i; /* Found it; return index */
	}

	return VMM_ENODATA;
}

int vmm_devtree_count_strings(struct vmm_devtree_node *node,
			      const char *attrib)
{
	int i = 0;
	const char *p;
	size_t l = 0, total = 0;
	struct vmm_devtree_attr *attr = vmm_devtree_getattr(node, attrib);

	if (!attr)
		return VMM_EINVALID;
	if (!attr->value)
		return VMM_ENODATA;
	if (strnlen(attr->value, attr->len) >= attr->len)
		return VMM_EILSEQ;

	p = attr->value;

	for (i = 0; total < attr->len; total += l, p += l, i++)
		l = strlen(p) + 1;

	return i;
}

int vmm_devtree_string_index(struct vmm_devtree_node *node,
			     const char *attrib,
			     int index, const char **out)
{
	int i;
	size_t l;
	const char *p, *end;
	struct vmm_devtree_attr *attr = vmm_devtree_getattr(node, attrib);

	if (!attr || !out)
		return VMM_EINVALID;
	if (!attr->value)
		return VMM_ENODATA;
	if (index < 0)
		return VMM_EILSEQ;

	p = attr->value;
	end = p + attr->len;

	for (i = 0; p < end; i++, p += l) {
		l = strlen(p) + 1;
		if (p + l > end)
			return VMM_EILSEQ;
		if (i == index) {
			*out = p;
			return l - 1;
		}
	}

	return VMM_ENODATA;
}

const u32 *vmm_devtree_next_u32(struct vmm_devtree_attr *attr,
				const u32 *cur, u32 *val)
{
	const u32 *ret;

	if (!attr || !attr->value)
		return NULL;

	if (!cur) {
		ret = (const u32 *)attr->value;
	} else if (((const u32 *)attr->value <= cur) &&
		   (cur < ((const u32 *)attr->value + attr->len / sizeof(u32)))) {
		ret = cur++;
	} else {
		ret = NULL;
	}

	if (val && ret) {
		*val = *ret;
	}

	return ret;
}

const char *vmm_devtree_next_string(struct vmm_devtree_attr *attr,
				    const char *cur)
{
	const char *first, *last;

	if (!attr || !attr->value)
		return NULL;

	first = (const char *)attr->value;
	last = first + attr->len;

	if (!cur) {
		return attr->value;
	} else if ((first <= cur) && (cur < last)) {
		cur = cur + strlen(cur) + 1;
		return (cur < last) ? cur : NULL;
	}

	return NULL;
}

static int recursive_getpath(char **out, size_t *out_len,
			     const struct vmm_devtree_node *node)
{
	int rc;
	size_t len;

	if (!node) {
		return VMM_EINVALID;
	}

	if (node->parent) {
		rc = recursive_getpath(out, out_len, node->parent);
		if (rc) {
			return rc;
		}
		if (*out_len < 2) {
			return VMM_ENOSPC;
		}
		**out = VMM_DEVTREE_PATH_SEPARATOR;
		(*out) += 1;
		**out = '\0';
		*out_len -= 1;
	}

	len = strlen(node->name);

	if (*out_len < (len + 1)) {
		return VMM_ENOSPC;
	}

	strncpy(*out, node->name, len);
	(*out) += len;
	**out = '\0';
	*out_len -= len;

	return VMM_OK;
}

int vmm_devtree_getpath(char *out, size_t out_len,
			const struct vmm_devtree_node *node)
{
	int rc;
	char *out_ptr = out;
	size_t out_ptr_len = out_len;

	if (!node || !out || (out_len < 2)) {
		return VMM_EFAIL;
	}

	out[0] = 0;

	rc = recursive_getpath(&out_ptr, &out_ptr_len, node);
	if (rc) {
		return rc;
	}

	if (strcmp(out, "") == 0) {
		out[0] = VMM_DEVTREE_PATH_SEPARATOR;
		out[1] = '\0';
	}

	return VMM_OK;
}

struct vmm_devtree_node *vmm_devtree_getchild(struct vmm_devtree_node *node,
					      const char *path)
{
	u32 len;
	bool found;
	struct vmm_devtree_node *np, *tp, *child;

	if (!path || !node)
		return NULL;

	while (*path == VMM_DEVTREE_PATH_SEPARATOR) {
		path++;
	}

	np = node;
	while (*path) {
		found = FALSE;
		vmm_devtree_for_each_child(child, np) {
			len = strlen(child->name);
			if (strncmp(child->name, path, len) == 0) {
				found = TRUE;
				path += len;
				if (*path) {
					if (*path != VMM_DEVTREE_PATH_SEPARATOR
					    && *(path + 1) != '\0') {
						path -= len;
						continue;
					}
					if (*path == VMM_DEVTREE_PATH_SEPARATOR)
						path++;
				}
				break;
			}
		}
		if (!found) {
			for (tp = np; tp && (tp != node); tp = tp->parent) {
				vmm_devtree_dref_node(tp);
			}
			return NULL;
		}
		np = child;
	};

	if (np) {
		vmm_devtree_ref_node(np);
	}

	for (tp = np; tp && (tp != node); tp = tp->parent) {
		vmm_devtree_dref_node(tp);
	}

	return np;
}

struct vmm_devtree_node *vmm_devtree_getnode(const char *path)
{
	struct vmm_devtree_node *node = dtree_ctrl.root;

	if (!node)
		return NULL;

	if (!path) {
		vmm_devtree_ref_node(node);
		return node;
	}

	if (strncmp(node->name, path, strlen(node->name)) != 0)
		return NULL;

	path += strlen(node->name);

	if (*path) {
		if (*path != VMM_DEVTREE_PATH_SEPARATOR && *(path + 1) != '\0')
			return NULL;
		if (*path == VMM_DEVTREE_PATH_SEPARATOR)
			path++;
	}

	return vmm_devtree_getchild(node, path);
}

const struct vmm_devtree_nodeid *vmm_devtree_match_node(
					const struct vmm_devtree_nodeid *matches,
					const struct vmm_devtree_node *node)
{
	const char *type;

	if (!matches || !node) {
		return NULL;
	}

	type = vmm_devtree_attrval(node, VMM_DEVTREE_DEVICE_TYPE_ATTR_NAME);

	while (matches->name[0] || matches->type[0] || matches->compatible[0]) {
		int match = 1;
		if (matches->name[0])
			match &= !strcmp(matches->name, node->name);
		if (matches->type[0])
			match &= type && !strcmp(matches->type, type);
		if (matches->compatible[0])
			match &= devtree_node_is_compatible(node,
							matches->compatible);
		if (match)
			return matches;
		matches++;
	}

	return NULL;
}

struct vmm_devtree_node *vmm_devtree_find_matching(
				struct vmm_devtree_node *node,
				const struct vmm_devtree_nodeid *matches)
{
	struct vmm_devtree_node *child, *ret;

	if (!matches) {
		return NULL;
	}

	if (!node) {
		node = dtree_ctrl.root;
	}

	if (vmm_devtree_match_node(matches, node)) {
		vmm_devtree_ref_node(node);
		return node;
	}

	vmm_devtree_for_each_child(child, node) {
		ret = vmm_devtree_find_matching(child, matches);
		if (ret) {
			vmm_devtree_dref_node(child);
			return ret;
		}
	}

	return NULL;
}

void vmm_devtree_iterate_matching(struct vmm_devtree_node *node,
				  const struct vmm_devtree_nodeid *matches,
				  void (*found)(struct vmm_devtree_node *node,
				      const struct vmm_devtree_nodeid *match,
				      void *data),
				  void *found_data)
{
	struct vmm_devtree_node *child;
	const struct vmm_devtree_nodeid *mid;

	if (!found || !matches) {
		return;
	}

	if (!node) {
		node = dtree_ctrl.root;
	}

	vmm_devtree_ref_node(node);

	mid = vmm_devtree_match_node(matches, node);
	if (mid) {
		found(node, mid, found_data);
	}

	vmm_devtree_for_each_child(child, node) {
		vmm_devtree_iterate_matching(child, matches,
					     found, found_data);
	}

	vmm_devtree_dref_node(node);
}

struct vmm_devtree_node *vmm_devtree_find_compatible(
				struct vmm_devtree_node *node,
				const char *device_type,
				const char *compatible)
{
	struct vmm_devtree_nodeid id[2];

	if (!compatible) {
		return NULL;
	}

	memset(id, 0, sizeof(id));

	if (device_type) {
		if (strlcpy(id[0].type, device_type, sizeof(id[0].type)) >=
		    sizeof(id[0].type)) {
			return NULL;
		}
	}

	if (strlcpy(id[0].compatible, compatible, sizeof(id[0].compatible)) >=
	    sizeof(id[0].compatible)) {
		return NULL;
	}

	return vmm_devtree_find_matching(node, id);
}

bool vmm_devtree_is_compatible(const struct vmm_devtree_node *node,
			       const char *compatible)
{
	struct vmm_devtree_nodeid id[2];

	if (!node || !compatible) {
		return FALSE;
	}

	memset(id, 0, sizeof(id));

	if (strlcpy(id[0].compatible, compatible, sizeof(id[0].compatible)) >=
		    sizeof(id[0].compatible)) {
		return FALSE;
	}

	return vmm_devtree_match_node(id, node) ? TRUE : FALSE;
}

static struct vmm_devtree_node *recursive_find_node_by_phandle(
					struct vmm_devtree_node *node,
					u32 phandle)
{
	int rc;
	u32 phnd;
	struct vmm_devtree_node *child, *ret;

	if (!node) {
		return NULL;
	}

	rc = vmm_devtree_read_u32(node, VMM_DEVTREE_PHANDLE_ATTR_NAME, &phnd);
	if ((rc == VMM_OK) && (phnd == phandle)) {
		ret = node;
		goto done;
	}

	ret = NULL;
	vmm_devtree_for_each_child(child, node) {
		ret = recursive_find_node_by_phandle(child, phandle);
		if (ret) {
			vmm_devtree_dref_node(child);
			break;
		}
	}

done:
	if (ret) {
		vmm_devtree_ref_node(ret);
	}

	return ret;
}

struct vmm_devtree_node *vmm_devtree_find_node_by_phandle(u32 phandle)
{
	if (!dtree_ctrl.root) {
		return NULL;
	}
	return recursive_find_node_by_phandle(dtree_ctrl.root, phandle);
}

static int devtree_parse_phandle_with_args(
					const struct vmm_devtree_node *np,
					const char *list_name,
					const char *cells_name,
					int cell_count, int index,
					struct vmm_devtree_phandle_args *out)
{
	u32 count = 0, phandle;
	int rc = 0, size, cur_index = 0;
	const u32 *list, *list_end, *cells_val;
	struct vmm_devtree_node *node = NULL;

	/* Retrieve the phandle list property */
	list = vmm_devtree_attrval(np, list_name);
	if (!list) {
		return VMM_ENOENT;
	}
	size = vmm_devtree_attrlen(np, list_name);
	list_end = list + size / sizeof(*list);

	/* Loop over the phandles until all the requested entry is found */
	while (list < list_end) {
		rc = VMM_EINVALID;
		count = 0;

		/*
		 * If phandle is 0, then it is an empty entry with no
		 * arguments.  Skip forward to the next entry.
		 */
		phandle = vmm_be32_to_cpu(*list);
		list++;
		if (phandle) {
			/*
			 * Find the provider node and parse the #*-cells
			 * property to determine the argument length.
			 *
			 * This is not needed if the cell count is hard-coded
			 * (i.e. cells_name not set, but cell_count is set),
			 * except when we're going to return the found node
			 * below.
			 */
			if (cells_name || cur_index == index) {
				node =
				   vmm_devtree_find_node_by_phandle(phandle);
				if (!node) {
					vmm_printf("%s: phandle not found\n",
						   np->name);
					goto err;
				}
			}

			if (cells_name) {
				cells_val =
					vmm_devtree_attrval(node, cells_name);
				if (!cells_val) {
					vmm_printf("%s: could not get "
						   "%s for %s\n",
						np->name, cells_name,
						node->name);
					goto err;
				}
				count = vmm_be32_to_cpu(*cells_val);
			} else {
				count = cell_count;
			}

			/*
			 * Make sure that the arguments actually fit in the
			 * remaining property data length
			 */
			if (list + count > list_end) {
				vmm_printf("%s: args longer than attribute\n",
					 np->name);
				goto err;
			}
		}

		/*
		 * All of the error cases above bail out of the loop, so at
		 * this point, the parsing is successful. If the requested
		 * index matches, then fill the out_args structure and return,
		 * or return VMM_ENOENT for an empty entry.
		 */
		rc = VMM_ENOENT;
		if (cur_index == index) {
			if (!phandle) {
				goto err;
			}

			if (out) {
				int i;
				if (WARN_ON(count > VMM_MAX_PHANDLE_ARGS))
					count = VMM_MAX_PHANDLE_ARGS;
				vmm_devtree_ref_node(node);
				out->np = node;
				out->args_count = count;
				for (i = 0; i < count; i++) {
					out->args[i] = vmm_be32_to_cpu(*list);
					list++;
				}
			}

			/* Found it! return success */
			return VMM_OK;
		}

		node = NULL;
		list += count;
		cur_index++;
	}

	/*
	 * Unlock node before returning result; will be one of:
	 * VMM_ENOENT : index is for empty phandle
	 * VMM_EINVAL : parsing error on data
	 * [1..n]  : Number of phandle (count mode; when index = -1)
	 */
	rc = index < 0 ? cur_index : VMM_ENOENT;
 err:
	return rc;
}

struct vmm_devtree_node *vmm_devtree_parse_phandle(
					const struct vmm_devtree_node *node,
					const char *phandle_name,
					int index)
{
	struct vmm_devtree_phandle_args args;

	if (index < 0)
		return NULL;

	if (devtree_parse_phandle_with_args(node, phandle_name, NULL,
					    0, index, &args))
		return NULL;

	return args.np;
}

int vmm_devtree_parse_phandle_with_args(const struct vmm_devtree_node *node,
					const char *list_name,
					const char *cells_name,
					int index,
					struct vmm_devtree_phandle_args *out)
{
	if (index < 0)
		return VMM_EINVALID;
	return devtree_parse_phandle_with_args(node, list_name, cells_name,
						0, index, out);
}

int vmm_devtree_parse_phandle_with_fixed_args(
					const struct vmm_devtree_node *node,
					const char *list_name,
					int cells_count,
					int index,
					struct vmm_devtree_phandle_args *out)
{
	if (index < 0)
		return VMM_EINVALID;
	return devtree_parse_phandle_with_args(node, list_name, NULL,
						cells_count, index, out);
}

int vmm_devtree_count_phandle_with_args(const struct vmm_devtree_node *node,
					const char *list_name,
					const char *cells_name)
{
	return devtree_parse_phandle_with_args(node, list_name, cells_name,
						0, -1, NULL);
}

struct vmm_devtree_node *vmm_devtree_ref_node(struct vmm_devtree_node *node)
{
	if (!node) {
		return NULL;
	}

	xref_get(&node->ref_count);

	return node;
}

static void __devtree_node_free(struct xref *ref)
{
	int rc;
	irq_flags_t flags;
	struct vmm_devtree_attr *attr, *attr_next;
	struct vmm_devtree_node *parent;
	struct vmm_devtree_node *node =
		container_of(ref, struct vmm_devtree_node, ref_count);

	if (dtree_ctrl.root == node) {
		dtree_ctrl.root = NULL;
	}

	vmm_read_lock_irqsave_lite(&node->attr_lock, flags);
	list_for_each_entry_safe(attr, attr_next,
				 &node->attr_list, head) {
		vmm_read_unlock_irqrestore_lite(&node->attr_lock, flags);
		if ((rc = vmm_devtree_delattr(node, attr->name))) {
			vmm_printf("%s: Failed to delete attibute=%s "
				   "from node=%s (error %d)\n",
				   __func__, attr->name, node->name, rc);
		}
		vmm_read_lock_irqsave_lite(&node->attr_lock, flags);
	}
	vmm_read_unlock_irqrestore_lite(&node->attr_lock, flags);

	if (node->parent) {
		parent = node->parent;
		vmm_write_lock_irqsave_lite(&parent->child_lock, flags);
		list_del(&node->head);
		vmm_write_unlock_irqrestore_lite(&parent->child_lock, flags);
		node->parent = NULL;
		vmm_devtree_dref_node(parent);
	}

	vmm_free(node);
}

void vmm_devtree_dref_node(struct vmm_devtree_node *node)
{
	if (node) {
		xref_put(&node->ref_count, __devtree_node_free);
	}
}

bool vmm_devtree_have_child(const struct vmm_devtree_node *node)
{
	bool ret;
	irq_flags_t flags;
	struct vmm_devtree_node *np = (struct vmm_devtree_node *)node;

	if (!np) {
		return FALSE;
	}

	vmm_read_lock_irqsave_lite(&np->child_lock, flags);
	ret = list_empty(&np->child_list) ? FALSE : TRUE;
	vmm_read_unlock_irqrestore_lite(&np->child_lock, flags);

	return ret;
}

struct vmm_devtree_node *vmm_devtree_next_child(
					const struct vmm_devtree_node *node,
					struct vmm_devtree_node *current)
{
	irq_flags_t flags;
	struct vmm_devtree_node *ret = NULL;
	struct vmm_devtree_node *np = (struct vmm_devtree_node *)node;

	if (!np) {
		return NULL;
	}

	vmm_read_lock_irqsave_lite(&np->child_lock, flags);
	if (!current) {
		if (!list_empty(&np->child_list)) {
			ret = list_first_entry(&np->child_list,
						struct vmm_devtree_node,
						head);
		}
	} else if ((current->parent == np) &&
		   !list_is_last(&current->head, &np->child_list)) {
		ret = list_first_entry(&current->head,
					struct vmm_devtree_node,
					head);
	}
	if (ret) {
		vmm_devtree_ref_node(ret);
	}
	vmm_read_unlock_irqrestore_lite(&np->child_lock, flags);

	if (current) {
		vmm_devtree_dref_node(current);
	}

	return ret;
}

struct vmm_devtree_node *vmm_devtree_get_child_by_name(
					struct vmm_devtree_node *node,
					const char *name)
{
	struct vmm_devtree_node *ret = NULL, *child = NULL;

	vmm_devtree_for_each_child(child, node) {
		if (strcasecmp(child->name, name) == 0) {
			ret = child;
			break;
		}
	}

	return ret;
}

struct vmm_devtree_node *vmm_devtree_addnode(struct vmm_devtree_node *parent,
					     const char *name)
{
	irq_flags_t flags;
	struct vmm_devtree_node *node = NULL;

	if (!name) {
		return NULL;
	}

	if (!parent) {
		parent = dtree_ctrl.root;
	}

	if (parent) {
		vmm_devtree_for_each_child(node, parent) {
			if (strcmp(node->name, name) == 0) {
				vmm_devtree_dref_node(node);
				return NULL;
			}
		}
	}

	node = vmm_zalloc(sizeof(struct vmm_devtree_node));
	if (!node) {
		return NULL;
	}
	INIT_LIST_HEAD(&node->head);
	INIT_RW_LOCK(&node->attr_lock);
	INIT_LIST_HEAD(&node->attr_list);
	INIT_RW_LOCK(&node->child_lock);
	INIT_LIST_HEAD(&node->child_list);
	xref_init(&node->ref_count);
	strncpy(node->name, name, sizeof(node->name));
	node->parent = NULL;
	node->system_data = NULL;
	node->priv = NULL;

	if (parent) {
		vmm_devtree_ref_node(parent);
		node->parent = parent;
		vmm_write_lock_irqsave_lite(&parent->child_lock, flags);
		list_add_tail(&node->head, &parent->child_list);
		vmm_write_unlock_irqrestore_lite(&parent->child_lock, flags);
	}

	return node;
}

static int devtree_copynode_recursive(struct vmm_devtree_node *dst,
				      struct vmm_devtree_node *src)
{
	int rc;
	struct vmm_devtree_attr *attr = NULL;
	struct vmm_devtree_node *child = NULL;
	struct vmm_devtree_node *schild = NULL;

	vmm_devtree_for_each_attr(attr, src) {
		if ((rc = vmm_devtree_setattr(dst, attr->name, attr->value,
					      attr->type, attr->len, TRUE))) {
			return rc;
		}
	}

	vmm_devtree_for_each_child(schild, src) {
		child = vmm_devtree_addnode(dst, schild->name);
		if (!child) {
			vmm_devtree_dref_node(schild);
			return VMM_EFAIL;
		}
		if ((rc = devtree_copynode_recursive(child, schild))) {
			vmm_devtree_dref_node(schild);
			return rc;
		}
	}
	
	return VMM_OK;
}

int vmm_devtree_copynode(struct vmm_devtree_node *parent,
			 const char *name,
			 struct vmm_devtree_node *src)
{
	struct vmm_devtree_node *node = NULL;

	if (!parent || !name || !src) {
		return VMM_EFAIL;
	}

	node = parent;
	while (node && src != node) {
		node = node->parent;
	}
	if (src == node) {
		return VMM_EFAIL;
	}
	node = NULL;

	node = vmm_devtree_addnode(parent, name);
	if (!node) {
		return VMM_EFAIL;
	}

	return devtree_copynode_recursive(node, src);
}

int vmm_devtree_delnode(struct vmm_devtree_node *node)
{
	int rc;
	irq_flags_t flags;
	struct vmm_devtree_node *child, *child_next;

	if (!node) {
		return VMM_EFAIL;
	}

	vmm_read_lock_irqsave_lite(&node->child_lock, flags);
	list_for_each_entry_safe(child, child_next,
				 &node->child_list, head) {
		vmm_read_unlock_irqrestore_lite(&node->child_lock, flags);
		if ((rc = vmm_devtree_delnode(child))) {
			return rc;
		}
		vmm_read_lock_irqsave_lite(&node->child_lock, flags);
	}
	vmm_read_unlock_irqrestore_lite(&node->child_lock, flags);

	vmm_devtree_dref_node(node);

	return VMM_OK;
}

int vmm_devtree_clock_frequency(struct vmm_devtree_node *node,
				u32 *clock_freq)
{
	if (!node || !clock_freq) {
		return VMM_EFAIL;
	}

	return vmm_devtree_read_u32(node,
			VMM_DEVTREE_CLOCK_FREQ_ATTR_NAME, clock_freq);
}

bool vmm_devtree_is_available(const struct vmm_devtree_node *node)
{
	const char *stat;
	u32 statlen;

	if (!node) {
		return FALSE;
	}

	stat = vmm_devtree_attrval(node, "status");
	if (stat == NULL) {
		return TRUE;
	}
	statlen = vmm_devtree_attrlen(node, "status");

	if (statlen > 0) {
		if (!strcmp(stat, "okay") || !strcmp(stat, "ok")) {
			return TRUE;
		}
	}

	return FALSE;
}

int vmm_devtree_alias_get_id(struct vmm_devtree_node *node,
			     const char *stem)
{
	int id;
	struct vmm_devtree_attr *attr;
	struct vmm_devtree_node *aliases;

	aliases = vmm_devtree_getnode(VMM_DEVTREE_PATH_SEPARATOR_STRING
				      VMM_DEVTREE_ALIASES_NODE_NAME);
	if (!aliases) {
		return VMM_ENODEV;
	}

	id = VMM_ENODEV;
	vmm_devtree_for_each_attr(attr, aliases) {
		const char *start = attr->name;
		const char *end = start + strlen(start);
		struct vmm_devtree_node *np;
		int len;

		/* Skip those we do not want to proceed */
		if (!strcmp(attr->name, "name") ||
		    !strcmp(attr->name, "phandle") ||
		    !strcmp(attr->name, "linux,phandle")) {
			continue;
		}

		/* Find the node by path */
		np = vmm_devtree_getnode(attr->value);
		if (!np) {
			continue;
		}

		/* Found node should be same as given node */
		if (node != np) {
			vmm_devtree_dref_node(np);
			continue;
		}
		vmm_devtree_dref_node(np);

		/* Walk the alias backwards to extract the id and
		 * work out the 'stem' string
		 */
		while (isdigit(*(end-1)) && end > start) {
			end--;
		}
		len = end - start;

		/* Compare stem to alias attribute name */
		if (strncmp(stem, start, len)) {
			continue;
		}

		id = atoi(end);
		/* If id found then we are done */
		if (id >= 0) {
			break;
		}
	}

	return id;
}

u32 vmm_devtree_nidtbl_count(void)
{
	return dtree_ctrl.nidtbl_count;
}

struct vmm_devtree_nidtbl_entry *vmm_devtree_nidtbl_get(int index)
{
	if ((index < 0) ||
	    (dtree_ctrl.nidtbl_count <= index)) {
		return NULL;
	}

	return &dtree_ctrl.nidtbl[index];
}

static bool devtree_compare_nid_for_matches(const char *subsys,
					    struct vmm_devtree_nidtbl_entry *nide)
{
	if (!subsys) {
		return TRUE;
	}

	return (strcmp(nide->subsys, subsys) == 0) ? TRUE : FALSE;
}

const struct vmm_devtree_nodeid *
			vmm_devtree_nidtbl_create_matches(const char *subsys)
{
	u32 i, idx, count;
	struct vmm_devtree_nodeid *nid, *matches;
	struct vmm_devtree_nidtbl_entry *nide;

	/* Count number of enteries to be put in matches table */
	count = 0;
	for (i = 0; i < dtree_ctrl.nidtbl_count; i++) {
		nide = &dtree_ctrl.nidtbl[i];
		if (devtree_compare_nid_for_matches(subsys, nide)) {
			count++;
		}
	}
	if (!count) {
		return NULL;
	}

	/* Alloc matches table with extra zero entry at the end */
	matches = vmm_zalloc((count + 1) * sizeof(struct vmm_devtree_nodeid));
	if (!matches) {
		return NULL;
	}

	/* Prepare matches table */
	idx = 0;
	for (i = 0; i < dtree_ctrl.nidtbl_count; i++) {
		if (count <= idx) {
			break;
		}
		nide = &dtree_ctrl.nidtbl[i];
		nid = &nide->nodeid;
		if (devtree_compare_nid_for_matches(subsys, nide)) {
			memcpy(&matches[idx], nid, sizeof(*nid));
			idx++;
		}
	}

	return matches;
}

void vmm_devtree_nidtbl_destroy_matches(
				const struct vmm_devtree_nodeid *matches)
{
	if (matches) {
		vmm_free((void *)matches);
	}
}

int __init vmm_devtree_init(void)
{
	int rc;
	u32 nidtbl_cnt;
	virtual_addr_t ca, nidtbl_va;
	virtual_size_t nidtbl_sz;
	struct vmm_devtree_nidtbl_entry *nide, *tnide;

	/* Reset the control structure */
	memset(&dtree_ctrl, 0, sizeof(dtree_ctrl));

	/* Populate Board Specific Device Tree */
	rc = arch_devtree_populate(&dtree_ctrl.root);
	if (rc) {
		return rc;
	}

	/* Populate nodeid table */
	nidtbl_va = arch_nidtbl_vaddr();
	nidtbl_sz = arch_nidtbl_size();
	if (!nidtbl_sz) {
		return VMM_OK;
	}
	nidtbl_cnt = udiv64(nidtbl_sz, sizeof(*tnide));
	dtree_ctrl.nidtbl = vmm_zalloc(nidtbl_cnt * sizeof(*tnide));
	if (!dtree_ctrl.nidtbl) {
		return VMM_ENOMEM;
	}
	dtree_ctrl.nidtbl_count = 0;
	for (ca = nidtbl_va; ca < (nidtbl_va + nidtbl_sz); ) {
		if (*(u32 *)ca != VMM_DEVTREE_NIDTBL_SIGNATURE) {
			ca += sizeof(u32);
			continue;
		}

		nide = (struct vmm_devtree_nidtbl_entry *)ca;
		tnide = &dtree_ctrl.nidtbl[dtree_ctrl.nidtbl_count];

		memcpy(tnide, nide, sizeof(*tnide));

		dtree_ctrl.nidtbl_count++;
		ca += sizeof(*nide);
	}

	return VMM_OK;
}
