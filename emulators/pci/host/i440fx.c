/**
 * Copyright (c) 2014 Himanshu Chauhan.
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
 * @file i440fx.c
 * @author Himanshu Chauhan (hschauhan@nulltrace.org)
 * @brief i440FX PCI and Memory Controller Emulator
 */

#include <vmm_error.h>
#include <vmm_heap.h>
#include <vmm_modules.h>
#include <vmm_devtree.h>
#include <vmm_devemu.h>
#include <vmm_timer.h>
#include <vmm_types.h>
#include <vmm_mutex.h>
#include <vmm_compiler.h>
#include <vmm_notifier.h>
#include <vmm_guest_aspace.h>
#include <vio/vmm_vserial.h>
#include <libs/stringlib.h>
#include <emu/pci/pci_emu_core.h>
#include <emu/pci/pci_ids.h>

#define I440FX_EMU_IPRIORITY		(PCI_EMU_CORE_IPRIORITY + 1)

#define MODULE_DESC			"i440FX Emulator"
#define MODULE_AUTHOR			"Himanshu Chauhan"
#define MODULE_LICENSE			"GPL"
#define MODULE_IPRIORITY		I440FX_EMU_IPRIORITY
#define	MODULE_INIT			i440fx_emulator_init
#define	MODULE_EXIT			i440fx_emulator_exit

enum {
	I440FX_LOG_LVL_ERR,
	I440FX_LOG_LVL_INFO,
	I440FX_LOG_LVL_DEBUG,
	I440FX_LOG_LVL_VERBOSE
};

enum {
      I440FX_PMC_CS_VID0 = 0x0,
      I440FX_PMC_CS_VID1 = 0x1,
      I440FX_PMC_CS_DID0 = 0x2,
      I440FX_PMC_CS_DID1 = 0x3,
      I440FX_PMC_CS_PCICMD0 = 0x4,
      I440FX_PMC_CS_PCICMD1 = 0x5,
      I440FX_PMC_CS_PCISTS0 = 0x6,
      I440FX_PMC_CS_PCISTS1 = 0x7,
      I440FX_PMC_CS_RID = 0x8,
      I440FX_PMC_CS_CLASSC0 = 0x9,
      I440FX_PMC_CS_CLASSC1 = 0xa,
      I440FX_PMC_CS_CLASSC2 = 0xb,
      I440FX_PMC_CS_RES0 = 0xc,
      I440FX_PMC_CS_MLT = 0xd,
      I440FX_PMC_CS_HEADT = 0xe,
      I440FX_PMC_CS_BIST = 0xf,
      I440FX_PMC_CS_PMCCFG0 = 0x50,
      I440FX_PMC_CS_PMCCFG1 = 0x51,
      I440FX_PMC_CS_DETURBO = 0x52,
      I440FX_PMC_CS_DBC = 0x53,
      I440FX_PMC_CS_AXC = 0x54,
      I440FX_PMC_CS_DRAMR0 = 0x55,
      I440FX_PMC_CS_DRAMR1 = 0x56,
      I440FX_PMC_CS_DRAMC = 0x57,
      I440FX_PMC_CS_DRAMT = 0x58,
      I440FX_PMC_CS_PAM0 = 0x59,
      I440FX_PMC_CS_PAM1 = 0x5a,
      I440FX_PMC_CS_PAM2 = 0x5b,
      I440FX_PMC_CS_PAM3 = 0x5c,
      I440FX_PMC_CS_PAM4 = 0x5d,
      I440FX_PMC_CS_PAM5 = 0x5e,
      I440FX_PMC_CS_PAM6 = 0x5f,
      I440FX_PMC_CS_ERRCMD = 0x90,
      I440FX_PMC_CS_ERRSTS = 0x91,
      I440FX_PMC_CS_TRC = 0x93
};

static int i440fx_default_log_lvl = I440FX_LOG_LVL_VERBOSE;

#define I440FX_LOG(lvl, fmt, args...)					\
	do {								\
		if (I440FX_LOG_##lvl <= i440fx_default_log_lvl) {	\
			vmm_printf("(%s:%d) " fmt, __func__,		\
				   __LINE__, ##args);			\
		}							\
	}while(0);

typedef struct i440fx_dev_registers {
	u16 pmc_cfg;
	u8 pam_regs[7];
} i440fx_dev_registers_t;

struct i440fx_state {
	struct vmm_mutex lock;
	struct vmm_guest *guest;
	struct vmm_devtree_node *node;
	struct pci_host_controller *controller;
	i440fx_dev_registers_t dev_regs;
	struct vmm_notifier_block guest_aspace_client;
	u32 conf_add;
	u32 conf_data;
};

static u32 i440fx_config_read(struct pci_class *pci_class, u16 reg_offset)
{
	struct pci_host_controller *pcntrl = (struct pci_host_controller *)pci_class;
	struct i440fx_state *s = container_of(&pcntrl, struct i440fx_state, controller);

	switch(reg_offset) {
		case I440FX_PMC_CS_PAM0 ... I440FX_PMC_CS_PAM6:
		vmm_printf("[%s]: Read from PAM%d\n", __func__, (reg_offset-I440FX_PMC_CS_PAM0));
		return s->dev_regs.pam_regs[reg_offset - I440FX_PMC_CS_PAM0];

	default:
		vmm_printf("[%s]: Read from register %d is not supported\n", __func__, reg_offset);
		return -1;
	}
	return -1;
}

static int i440fx_config_write(struct pci_class *pci_class, u16 reg_offset, u32 data)
{
#if 0 /* FIXME: populate this when i440fx is revisited. */
	struct pci_host_controller *pcntrl = (struct pci_host_controller *)pci_class;
	struct i440fx_state *s = container_of(&pcntrl, struct i440fx_state, controller);

	switch(reg_offset) {
	}
#endif
	return VMM_OK;
}

static int i440fx_reg_write(struct i440fx_state *s, u32 addr,
			    u32 src_mask, u32 val)
{
	s->conf_add = val;

	return VMM_OK;
}

static int i440fx_reg_read(struct i440fx_state *s, u32 addr, u32 *dst, u32 size)
{
	u16 bus, dev, func, reg_offs;
	struct pci_device *pdev;

	/*
	 * If guest is reading from command register,
	 * then its probing if PCI is supported or not.
	 */
	if (!addr) {
		*dst = s->conf_add;
		return VMM_OK;
	}

	if (s->conf_add & 0x80000000) {
		bus = (s->conf_add >> 16) & 0xff;
		dev = (s->conf_add >> 11) & 0x1f;
		func = (s->conf_add >> 8) & 0x7;
		reg_offs = (s->conf_add) & 0xfc;

		/* if bus and dev are 0, its bound to PMC */
		if (bus == 0 && dev == 0) {
			/* PMC is not a multi-function device */
			if (func) {
				*dst = 0xFFFFFFFFUL;
			} else {
				*dst = pci_emu_config_space_read((struct pci_class *)s->controller,
								 reg_offs, size);
			}
		} else {
			if (pci_emu_find_pci_device(s->controller, bus, dev, &pdev) != VMM_OK) {
				*dst = 0xffff;
				return VMM_OK;
			}

			*dst = pci_emu_config_space_read((struct pci_class *)pdev, reg_offs, size);
		}
	}

	return VMM_OK;
}

static int i440fx_emulator_reset(struct vmm_emudev *edev)
{
	return VMM_OK;
}

static int i440fx_emulator_read8(struct vmm_emudev *edev,
				 physical_addr_t offset,
				 u8 *dst)
{
	int rc;
	u32 regval = 0x0;

	rc = i440fx_reg_read(edev->priv, offset, &regval, 1);
	if (!rc) {
		*dst = regval & 0xFF;
	}

	return rc;
}

static int i440fx_emulator_read16(struct vmm_emudev *edev,
				  physical_addr_t offset,
				  u16 *dst)
{
	int rc;
	u32 regval = 0x0;

	rc = i440fx_reg_read(edev->priv, offset, &regval, 2);
	if (!rc) {
		*dst = regval & 0xFFFF;
	}

	return rc;
}

static int i440fx_emulator_read32(struct vmm_emudev *edev,
				  physical_addr_t offset,
				  u32 *dst)
{
	return i440fx_reg_read(edev->priv, offset, dst, 4);
}

static int i440fx_emulator_write8(struct vmm_emudev *edev,
				   physical_addr_t offset,
				   u8 src)
{
	return i440fx_reg_write(edev->priv, offset, 0xFFFFFF00, src);
}

static int i440fx_emulator_write16(struct vmm_emudev *edev,
				    physical_addr_t offset,
				    u16 src)
{
	return i440fx_reg_write(edev->priv, offset, 0xFFFF0000, src);
}

static int i440fx_emulator_write32(struct vmm_emudev *edev,
				   physical_addr_t offset,
				   u32 src)
{
	return i440fx_reg_write(edev->priv, offset, 0x00000000, src);
}

static int i440fx_guest_aspace_notification(struct vmm_notifier_block *nb,
					    unsigned long evt, void *data)
{
	int ret = NOTIFY_DONE;
	int rc;
	struct i440fx_state *i440fx =
		container_of(nb, struct i440fx_state, guest_aspace_client);

	vmm_mutex_lock(&i440fx->lock);

	switch (evt) {
	case VMM_GUEST_ASPACE_EVENT_RESET:
		if ((rc =
		     pci_emu_register_controller(i440fx->node,
						 i440fx->guest,
						 i440fx->controller))
		    != VMM_OK) {
			I440FX_LOG(LVL_ERR,
				   "Failed to attach PCI controller.\n");
			goto _failed;
		}
		ret = NOTIFY_OK;
		break;
	default:
		break;
	}

 _failed:
	vmm_mutex_unlock(&i440fx->lock);

	return ret;
}

static int i440fx_emulator_probe(struct vmm_guest *guest,
				 struct vmm_emudev *edev,
				 const struct vmm_devtree_nodeid *eid)
{
	int rc = VMM_OK, i;
	char name[64];
	struct i440fx_state *s;
	struct pci_class *class;

	s = vmm_zalloc(sizeof(struct i440fx_state));
	if (!s) {
		I440FX_LOG(LVL_ERR, "Failed to allocate i440fx's state.\n");
		rc = VMM_EFAIL;
		goto _failed;
	}

	s->node = edev->node;
	s->guest = guest;
	s->dev_regs.pam_regs[0] = 0x33; /* BIOS read/write enable */
	s->dev_regs.pam_regs[1] = 0x0; /* no read/write */
	s->dev_regs.pam_regs[2] = 0x0;
	s->dev_regs.pam_regs[3] = 0x0;
	s->dev_regs.pam_regs[4] = 0x0;
	s->dev_regs.pam_regs[5] = 0x33;
	s->dev_regs.pam_regs[6] = 0x33; /* read/write enable to BIOS extension */
	s->controller = vmm_zalloc(sizeof(struct pci_host_controller));
	if (!s->controller) {
		I440FX_LOG(LVL_ERR, "Failed to allocate pci host contoller for i440fx.\n");
		goto _failed;
	}
	INIT_MUTEX(&s->lock);
	INIT_LIST_HEAD(&s->controller->head);
	INIT_LIST_HEAD(&s->controller->attached_buses);

	/* initialize class */
	class = (struct pci_class *)s->controller;
	INIT_SPIN_LOCK(&class->lock);
	class->conf_header.vendor_id = PCI_VENDOR_ID_INTEL;
	class->conf_header.device_id = PCI_DEVICE_ID_INTEL_82441;
	class->conf_header.class = PCI_BASE_CLASS_BRIDGE;
	class->conf_header.revision = 2;
	class->config_read = i440fx_config_read;
	class->config_write = i440fx_config_write;

	rc = vmm_devtree_read_u32(edev->node, "nr_buses",
				  &s->controller->nr_buses);
	if (rc) {
		I440FX_LOG(LVL_ERR, "Failed to get fifo size in guest DTS.\n");
		goto _failed;
	}

	I440FX_LOG(LVL_VERBOSE, "%s: %d busses on this controller.\n",
		   __func__, s->controller->nr_buses);

	for (i = 0; i < s->controller->nr_buses; i++) {
		if ((rc = pci_emu_attach_new_pci_bus(s->controller, i))
		    != VMM_OK) {
			I440FX_LOG(LVL_ERR, "Failed to attach PCI bus %d\n",
				   i+1);
			goto _failed;
		}
	}

	strlcpy(name, guest->name, sizeof(name));
	strlcat(name, "/", sizeof(name));
	if (strlcat(name, edev->node->name, sizeof(name)) >= sizeof(name)) {
		rc = VMM_EOVERFLOW;
		goto _failed;
	}

	edev->priv = s;

	s->guest_aspace_client.notifier_call = &i440fx_guest_aspace_notification;
	s->guest_aspace_client.priority = 0;

	vmm_guest_aspace_register_client(&s->guest_aspace_client);

	I440FX_LOG(LVL_VERBOSE, "Success.\n");

	goto _done;

 _failed:
	if (s && s->controller) vmm_free(s->controller);
	if (s) vmm_free(s);

 _done:
	return rc;
}

static int i440fx_emulator_remove(struct vmm_emudev *edev)
{
	//struct i440fx_state *s = edev->priv;

	return VMM_OK;
}

static struct vmm_devtree_nodeid i440fx_emuid_table[] = {
	{
		.type = "pci-host-controller",
		.compatible = "i440fx",
	},
	{ /* end of list */ },
};

static struct vmm_emulator i440fx_emulator = {
	.name = "i440fx_emulator",
	.match_table = i440fx_emuid_table,
	.endian = VMM_DEVEMU_LITTLE_ENDIAN,
	.probe = i440fx_emulator_probe,
	.read8 = i440fx_emulator_read8,
	.write8 = i440fx_emulator_write8,
	.read16 = i440fx_emulator_read16,
	.write16 = i440fx_emulator_write16,
	.read32 = i440fx_emulator_read32,
	.write32 = i440fx_emulator_write32,
	.reset = i440fx_emulator_reset,
	.remove = i440fx_emulator_remove,
};

static int __init i440fx_emulator_init(void)
{
	return vmm_devemu_register_emulator(&i440fx_emulator);
}

static void __exit i440fx_emulator_exit(void)
{
	vmm_devemu_unregister_emulator(&i440fx_emulator);
}

VMM_DECLARE_MODULE(MODULE_DESC,
		   MODULE_AUTHOR,
		   MODULE_LICENSE,
		   MODULE_IPRIORITY,
		   MODULE_INIT,
		   MODULE_EXIT);
