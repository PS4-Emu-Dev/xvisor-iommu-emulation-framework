/**
 * Copyright (C) 2016 Anup Patel.
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
 * @file vmm_msi.c
 * @author Anup Patel (anup@brainfault.org)
 * @brief Generic Host MSI framework implementation.
 */

#include <vmm_error.h>
#include <vmm_heap.h>
#include <vmm_host_irq.h>
#include <vmm_host_irqdomain.h>
#include <vmm_spinlocks.h>
#include <vmm_msi.h>
#include <libs/stringlib.h>

static DEFINE_SPINLOCK(msi_lock);
static LIST_HEAD(msi_domain_list);

static int msi_domain_ops_init(struct vmm_msi_domain *domain,
			       unsigned int hirq, unsigned int hwirq,
			       vmm_msi_alloc_info_t *arg)
{
	return 0;
}

static void msi_domain_ops_free(struct vmm_msi_domain *domain,
				unsigned int hirq)
{
}

static int msi_domain_ops_check(struct vmm_msi_domain *domain,
				struct vmm_device *dev)
{
	return 0;
}

static int msi_domain_ops_prepare(struct vmm_msi_domain *domain,
				  struct vmm_device *dev,
				  int nvec, vmm_msi_alloc_info_t *arg)
{
	memset(arg, 0, sizeof(*arg));
	return 0;
}

static void msi_domain_ops_finish(vmm_msi_alloc_info_t *arg, int retval)
{
}

static void msi_domain_ops_set_desc(vmm_msi_alloc_info_t *arg,
				    struct vmm_msi_desc *desc)
{
	arg->desc = desc;
}

static int msi_domain_ops_handle_error(struct vmm_msi_domain *domain,
				       struct vmm_msi_desc *desc, int error)
{
	return error;
}

static void msi_domain_ops_write_msg(struct vmm_msi_domain *domain,
				     struct vmm_msi_desc *desc,
				     unsigned int hirq, unsigned int hwirq,
				     struct vmm_msi_msg *msg)
{
}

static struct vmm_msi_domain_ops msi_domain_ops_default = {
	.msi_init	= msi_domain_ops_init,
	.msi_free	= msi_domain_ops_free,
	.msi_check	= msi_domain_ops_check,
	.msi_prepare	= msi_domain_ops_prepare,
	.msi_finish	= msi_domain_ops_finish,
	.set_desc	= msi_domain_ops_set_desc,
	.handle_error	= msi_domain_ops_handle_error,
	.msi_write_msg	= msi_domain_ops_write_msg,
};

static void vmm_msi_domain_update_dom_ops(struct vmm_msi_domain *domain)
{
	struct vmm_msi_domain_ops *ops = domain->ops;

	if (ops == NULL) {
		domain->ops = &msi_domain_ops_default;
		return;
	}

	if (ops->msi_init == NULL)
		ops->msi_init = msi_domain_ops_default.msi_init;
	if (ops->msi_free == NULL)
		ops->msi_free = msi_domain_ops_default.msi_free;
	if (ops->msi_check == NULL)
		ops->msi_check = msi_domain_ops_default.msi_check;
	if (ops->msi_prepare == NULL)
		ops->msi_prepare = msi_domain_ops_default.msi_prepare;
	if (ops->msi_finish == NULL)
		ops->msi_finish = msi_domain_ops_default.msi_finish;
	if (ops->set_desc == NULL)
		ops->set_desc = msi_domain_ops_default.set_desc;
	if (ops->handle_error == NULL)
		ops->handle_error = msi_domain_ops_default.handle_error;
	if (ops->msi_write_msg == NULL)
		ops->msi_write_msg = msi_domain_ops_default.msi_write_msg;
}

struct vmm_msi_desc *vmm_alloc_msi_entry(struct vmm_device *dev)
{
	struct vmm_msi_desc *desc = vmm_zalloc(sizeof(*desc));

	if (!desc)
		return NULL;

	INIT_LIST_HEAD(&desc->list);
	desc->dev = dev;

	return desc;
}

void vmm_free_msi_entry(struct vmm_msi_desc *entry)
{
	vmm_free(entry);
}

struct vmm_msi_domain *vmm_msi_create_domain(
					enum vmm_msi_domain_types type,
					struct vmm_devtree_node *fwnode,
					struct vmm_msi_domain_ops *ops,
					struct vmm_host_irqdomain *parent,
					unsigned long flags,
					void *data)
{
	irq_flags_t f;
	bool found = FALSE;
	struct vmm_msi_domain *domain, *d;

	if (type <= VMM_MSI_DOMAIN_UNKNOWN || VMM_MSI_DOMAIN_MAX <= type)
		return NULL;
	if (!fwnode || !ops || !parent)
		return NULL;

	domain = vmm_zalloc(sizeof(*domain));
	if (!domain)
		return NULL;

	INIT_LIST_HEAD(&domain->head);
	domain->type = type;
	vmm_devtree_ref_node(fwnode);
	domain->fwnode = fwnode;
	domain->ops = ops;
	domain->parent = parent;
	domain->flags = flags;
	domain->data = data;

	vmm_spin_lock_irqsave_lite(&msi_lock, f);

	list_for_each_entry(d, &msi_domain_list, head) {
		if (d->fwnode == fwnode && d->type == type) {
			found = TRUE;
			break;
		}
	}

	if (found) {
		vmm_spin_unlock_irqrestore_lite(&msi_lock, f);
		vmm_devtree_dref_node(domain->fwnode);
		vmm_free(domain);
		return NULL;
	}

	list_add_tail(&domain->head, &msi_domain_list);

	vmm_spin_unlock_irqrestore_lite(&msi_lock, f);

	if (domain->flags & VMM_MSI_FLAG_USE_DEF_DOM_OPS)
		vmm_msi_domain_update_dom_ops(domain);

	return domain;
}

void vmm_msi_destroy_domain(struct vmm_msi_domain *domain)
{
	irq_flags_t f;
	bool found = FALSE;
	struct vmm_msi_domain *d;

	if (!domain)
		return;

	vmm_spin_lock_irqsave_lite(&msi_lock, f);

	list_for_each_entry(d, &msi_domain_list, head) {
		if (d == domain) {
			found = TRUE;
			break;
		}
	}

	if (!found) {
		vmm_spin_unlock_irqrestore_lite(&msi_lock, f);
		return;
	}

	list_del(&domain->head);

	vmm_spin_unlock_irqrestore_lite(&msi_lock, f);

	vmm_devtree_dref_node(domain->fwnode);
	vmm_free(domain);
}

struct vmm_msi_domain *vmm_msi_find_domain(struct vmm_devtree_node *fwnode,
					   enum vmm_msi_domain_types type)
{
	irq_flags_t f;
	struct vmm_msi_domain *d, *domain = NULL;

	if (!fwnode)
		return NULL;

	vmm_spin_lock_irqsave_lite(&msi_lock, f);

	list_for_each_entry(d, &msi_domain_list, head) {
		if (d->fwnode == fwnode && d->type == type) {
			domain = d;
			break;
		}
	}

	vmm_spin_unlock_irqrestore_lite(&msi_lock, f);

	return domain;
}

void vmm_msi_domain_write_msg(struct vmm_host_irq *irq)
{
	struct vmm_msi_desc *desc = vmm_host_irq_get_msi_data(irq);
	struct vmm_msi_domain_ops *ops;
	struct vmm_msi_domain *domain;
	int ret;

	if (!desc || !desc->domain)
		return;
	domain = desc->domain;
	ops = domain->ops;

	memset(&desc->msg, 0, sizeof(desc->msg));
	ret = vmm_host_irq_compose_msi_msg(irq->num, &desc->msg);
	BUG_ON(ret < 0);
	ops->msi_write_msg(domain, desc,
			   irq->num, irq->hwirq, &desc->msg);
}

int vmm_msi_domain_alloc_irqs(struct vmm_msi_domain *domain,
			      struct vmm_device *dev,
			      int nvec)
{
	vmm_msi_alloc_info_t arg;
	struct vmm_msi_desc *desc;
	int i, ret = VMM_OK, hwirq, hirq = -1;
	struct vmm_msi_domain_ops *ops = domain->ops;

	ret = ops->msi_check(domain, dev);
	if (ret)
		return ret;
	ret = ops->msi_prepare(domain, dev, nvec, &arg);
	if (ret)
		return ret;

	for_each_msi_entry(desc, dev) {
		ops->set_desc(&arg, desc);

		hirq = vmm_host_irqdomain_alloc(domain->parent,
						desc->nvec_used, &arg);
		if (hirq < 0) {
			ret = VMM_ENOSPC;
			goto fail_handle_error;
		}
		hwirq = vmm_host_irqdomain_to_hwirq(domain->parent, hirq);
		desc->hirq = hirq;
		desc->domain = domain;

		for (i = 0; i < desc->nvec_used; i++) {
			vmm_host_irq_set_msi_data(hirq + i, desc);
			ret = ops->msi_init(domain, hirq + i,
					    hwirq + i, &arg);
			if (ret < 0) {
				for (i--; i > 0; i--)
					ops->msi_free(domain, hirq + i);
				vmm_host_irqdomain_free(domain->parent,
						desc->hirq, desc->nvec_used);
				goto fail_handle_error;
			}
		}
	}

	ops->msi_finish(&arg, 0);

	/* If everything went fine then we write MSI messages */
	for_each_msi_entry(desc, dev) {
		for (i = 0; i < desc->nvec_used; i++) {
			vmm_msi_domain_write_msg(
					vmm_host_irq_get(desc->hirq + i));
		}
	}

	return VMM_OK;

fail_handle_error:
	ret = ops->handle_error(domain, desc, ret);
	ops->msi_finish(&arg, ret);
	return ret;
}

void vmm_msi_domain_free_irqs(struct vmm_msi_domain *domain,
			      struct vmm_device *dev)
{
	unsigned int i, hirq, hwirq;
	struct vmm_msi_desc *desc;
	struct vmm_msi_domain_ops *ops;

	if (!domain || !dev)
		return;
	ops = domain->ops;

	for_each_msi_entry(desc, dev) {
		/*
		 * We might have failed to allocate an MSI early enough
		 * that there is no host IRQ associated to this entry.
		 * If that's the case, don't do anything.
		 */
		if (!desc->hirq) {
			continue;
		}

		hirq = desc->hirq;
		hwirq = vmm_host_irqdomain_to_hwirq(domain->parent, hirq);

		memset(&desc->msg, 0, sizeof(desc->msg));

		for (i = 0; i < desc->nvec_used; i++) {
			ops->msi_write_msg(domain, desc,
					   hirq + i, hwirq + i, &desc->msg);
		}

		for (i = 0; i < desc->nvec_used; i++) {
			ops->msi_free(domain, hirq + i);
		}

		vmm_host_irqdomain_free(domain->parent,
					desc->hirq, desc->nvec_used);
		desc->hirq = 0;
	}
}
