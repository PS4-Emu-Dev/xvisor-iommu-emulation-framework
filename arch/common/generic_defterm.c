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
 * @file generic_defterm.c
 * @author Anup Patel (anup@brainfault.org)
 * @author Jean-Christophe Dubois (jcd@tribudubois.net)
 * @author Sukanto Ghosh (sukantoghosh@gmail.com)
 * @brief generic arch default terminal (defterm) functions using drivers
 */

#include <vmm_error.h>
#include <vmm_types.h>
#include <vmm_compiler.h>
#include <vmm_devtree.h>
#include <vmm_host_aspace.h>
#include <generic_defterm.h>
#include <arch_defterm.h>

static int unknown_defterm_putc(u8 ch)
{
	return VMM_EFAIL;
}

static int unknown_defterm_getc(u8 *ch)
{
	return VMM_EFAIL;
}

static int __init unknown_defterm_init(struct vmm_devtree_node *node)
{
	return VMM_ENODEV;
}

static struct defterm_ops unknown_ops = {
	.putc = unknown_defterm_putc,
	.getc = unknown_defterm_getc,
	.init = unknown_defterm_init
};

#if defined(CONFIG_SERIAL_PL01X)

#include <drv/serial/pl011.h>

static virtual_addr_t pl011_defterm_base;
static bool pl011_defterm_skip_baud_config;
static u32 pl011_defterm_inclk;
static u32 pl011_defterm_baud;

static int pl011_defterm_putc(u8 ch)
{
	if (!pl011_lowlevel_can_putc(pl011_defterm_base)) {
		return VMM_EFAIL;
	}
	pl011_lowlevel_putc(pl011_defterm_base, ch);
	return VMM_OK;
}

static int pl011_defterm_getc(u8 *ch)
{
	if (!pl011_lowlevel_can_getc(pl011_defterm_base)) {
		return VMM_EFAIL;
	}
	*ch = pl011_lowlevel_getc(pl011_defterm_base);
	return VMM_OK;
}

static int __init pl011_defterm_init(struct vmm_devtree_node *node)
{
	int rc;

	rc = vmm_devtree_regmap(node, &pl011_defterm_base, 0);
	if (rc) {
		return rc;
	}

	rc = vmm_devtree_clock_frequency(node,
				&pl011_defterm_inclk);
	if (rc) {
		pl011_defterm_skip_baud_config = TRUE;
	} else {
		pl011_defterm_skip_baud_config = FALSE;
	}

	if (vmm_devtree_read_u32(node, "baudrate",
				&pl011_defterm_baud)) {
		pl011_defterm_baud = 115200;
	}

	pl011_lowlevel_init(pl011_defterm_base,
			    pl011_defterm_skip_baud_config,
			    pl011_defterm_baud,
			    pl011_defterm_inclk);

	return VMM_OK;
}

static struct defterm_ops pl011_ops = {
	.putc = pl011_defterm_putc,
	.getc = pl011_defterm_getc,
	.init = pl011_defterm_init
};

#else

#define pl011_ops unknown_ops

#endif

#if defined(CONFIG_SERIAL_8250_UART)

#include <drv/serial/8250-uart.h>

static struct uart_8250_port uart8250_port;

static int uart8250_defterm_putc(u8 ch)
{
	if (!uart_8250_lowlevel_can_putc(&uart8250_port)) {
		return VMM_EFAIL;
	}
	uart_8250_lowlevel_putc(&uart8250_port, ch);
	return VMM_OK;
}

static int uart8250_defterm_getc(u8 *ch)
{
	if (!uart_8250_lowlevel_can_getc(&uart8250_port)) {
		return VMM_EFAIL;
	}
	*ch = uart_8250_lowlevel_getc(&uart8250_port);
	return VMM_OK;
}

static int __init uart8250_defterm_init(struct vmm_devtree_node *node)
{
	int rc;

	rc = vmm_devtree_regmap(node, &uart8250_port.base, 0);
	if (rc) {
		return rc;
	}

	rc = vmm_devtree_clock_frequency(node,
				&uart8250_port.input_clock);
	if (rc) {
		uart8250_port.skip_baudrate_config = TRUE;
	} else {
		uart8250_port.skip_baudrate_config = FALSE;
	}

	if (vmm_devtree_read_u32(node, "baudrate",
				 &uart8250_port.baudrate)) {
		uart8250_port.baudrate = 115200;
	}

	if (vmm_devtree_read_u32(node, "reg-shift",
				 &uart8250_port.reg_shift)) {
		uart8250_port.reg_shift = 0;
	}

	if (vmm_devtree_read_u32(node, "reg-io-width",
				 &uart8250_port.reg_width)) {
		uart8250_port.reg_width = 1;
	}

	uart_8250_lowlevel_init(&uart8250_port);

	return VMM_OK;
}

static struct defterm_ops uart8250_ops = {
	.putc = uart8250_defterm_putc,
	.getc = uart8250_defterm_getc,
	.init = uart8250_defterm_init
};

#else

#define uart8250_ops unknown_ops

#endif

#if defined(CONFIG_SERIAL_OMAP_UART)

#include <drv/serial/omap-uart.h>

static virtual_addr_t omap_defterm_base;
static bool omap_defterm_skip_baud_config;
static u32 omap_defterm_inclk;
static u32 omap_defterm_baud;

static int omap_defterm_putc(u8 ch)
{
	if (!omap_uart_lowlevel_can_putc(omap_defterm_base, 2)) {
		return VMM_EFAIL;
	}
	omap_uart_lowlevel_putc(omap_defterm_base, 2, ch);
	return VMM_OK;
}

static int omap_defterm_getc(u8 *ch)
{
	if (!omap_uart_lowlevel_can_getc(omap_defterm_base, 2)) {
		return VMM_EFAIL;
	}
	*ch = omap_uart_lowlevel_getc(omap_defterm_base, 2);
	return VMM_OK;
}

static int __init omap_defterm_init(struct vmm_devtree_node *node)
{
	int rc;

	rc = vmm_devtree_regmap(node, &omap_defterm_base, 0);
	if (rc) {
		return rc;
	}

	rc = vmm_devtree_clock_frequency(node,
				&omap_defterm_inclk);
	if (rc) {
		omap_defterm_skip_baud_config = TRUE;
	} else {
		omap_defterm_skip_baud_config = FALSE;
	}

	if (vmm_devtree_read_u32(node, "baudrate",
				&omap_defterm_baud)) {
		omap_defterm_baud = 115200;
	}

	omap_uart_lowlevel_init(omap_defterm_base, 2,
				omap_defterm_skip_baud_config,
				omap_defterm_baud,
				omap_defterm_inclk);

	return VMM_OK;
}

static struct defterm_ops omapuart_ops = {
	.putc = omap_defterm_putc,
	.getc = omap_defterm_getc,
	.init = omap_defterm_init
};

#else

#define omapuart_ops unknown_ops

#endif

#if defined(CONFIG_SERIAL_IMX)

#include <drv/serial/imx-uart.h>

static virtual_addr_t imx_defterm_base;
static bool imx_defterm_skip_baudrate_config;
static u32 imx_defterm_inclk;
static u32 imx_defterm_baud;

static int imx_defterm_putc(u8 ch)
{
	if (!imx_lowlevel_can_putc(imx_defterm_base)) {
		return VMM_EFAIL;
	}
	imx_lowlevel_putc(imx_defterm_base, ch);
	return VMM_OK;
}

static int imx_defterm_getc(u8 *ch)
{
	if (!imx_lowlevel_can_getc(imx_defterm_base)) {
		return VMM_EFAIL;
	}
	*ch = imx_lowlevel_getc(imx_defterm_base);
	return VMM_OK;
}

static int __init imx_defterm_init(struct vmm_devtree_node *node)
{
	int rc;

	rc = vmm_devtree_regmap(node, &imx_defterm_base, 0);
	if (rc) {
		return rc;
	}

	rc = vmm_devtree_clock_frequency(node, &imx_defterm_inclk);
	if (rc) {
		imx_defterm_skip_baudrate_config = TRUE;
	} else {
		imx_defterm_skip_baudrate_config = FALSE;
	}

	if (vmm_devtree_read_u32(node, "baudrate",
				&imx_defterm_baud)) {
		imx_defterm_baud = 115200;
	}

	imx_lowlevel_init(imx_defterm_base,
			  imx_defterm_skip_baudrate_config,
			  imx_defterm_baud,
			  imx_defterm_inclk);

	return VMM_OK;
}

static struct defterm_ops imx_ops = {
	.putc = imx_defterm_putc,
	.getc = imx_defterm_getc,
	.init = imx_defterm_init
};

#else

#define imx_ops unknown_ops

#endif

#if defined(CONFIG_SERIAL_SAMSUNG)

#include <drv/serial/samsung-uart.h>

static virtual_addr_t samsung_defterm_base;
static bool samsung_defterm_skip_baud_config;
static u32 samsung_defterm_inclk;
static u32 samsung_defterm_baud;

static int samsung_defterm_putc(u8 ch)
{
	if (!samsung_lowlevel_can_putc(samsung_defterm_base)) {
		return VMM_EFAIL;
	}
	samsung_lowlevel_putc(samsung_defterm_base, ch);
	return VMM_OK;
}

static int samsung_defterm_getc(u8 *ch)
{
	if (!samsung_lowlevel_can_getc(samsung_defterm_base)) {
		return VMM_EFAIL;
	}
	*ch = samsung_lowlevel_getc(samsung_defterm_base);
	return VMM_OK;
}

static int __init samsung_defterm_init(struct vmm_devtree_node *node)
{
	int rc;

	/* map this console device */
	rc = vmm_devtree_regmap(node, &samsung_defterm_base, 0);
	if (rc) {
		return rc;
	}

	/* retrieve clock frequency */
	rc = vmm_devtree_clock_frequency(node,
				&samsung_defterm_inclk);
	if (rc) {
		samsung_defterm_skip_baud_config = TRUE;
	} else {
		samsung_defterm_skip_baud_config = FALSE;
	}
	
	/* retrieve baud rate */
	if (vmm_devtree_read_u32(node, "baudrate",
				&samsung_defterm_baud)) {
		samsung_defterm_baud = 115200;
	}

	/* initialize the console port */
	samsung_lowlevel_init(samsung_defterm_base,
			      samsung_defterm_skip_baud_config,
			      samsung_defterm_baud,
			      samsung_defterm_inclk);

	return VMM_OK;
}

static struct defterm_ops samsung_ops = {
	.putc = samsung_defterm_putc,
	.getc = samsung_defterm_getc,
	.init = samsung_defterm_init
};

#else

#define samsung_ops unknown_ops

#endif

#if defined(CONFIG_SERIAL_SCIF)

#include <drv/serial/scif.h>

static virtual_addr_t scif_defterm_base;
static bool scif_defterm_skip_baud_config;
static u32 scif_defterm_inclk;
static u32 scif_defterm_baud;
static unsigned long scif_regtype = SCIx_SH4_SCIF_BRG_REGTYPE;
static bool scif_defterm_use_intclk;

static int scif_defterm_putc(u8 ch)
{
	if (!scif_lowlevel_can_putc(scif_defterm_base, scif_regtype)) {
		return VMM_EFAIL;
	}
	scif_lowlevel_putc(scif_defterm_base, scif_regtype, ch);
	return VMM_OK;
}

static int scif_defterm_getc(u8 *ch)
{
	if (!scif_lowlevel_can_getc(scif_defterm_base, scif_regtype)) {
		return VMM_EFAIL;
	}
	*ch = scif_lowlevel_getc(scif_defterm_base, scif_regtype);
	return VMM_OK;
}

static int __init scif_defterm_init(struct vmm_devtree_node *node)
{
	int rc;

	rc = vmm_devtree_regmap(node, &scif_defterm_base, 0);
	if (rc) {
		return rc;
	}

	rc = vmm_devtree_clock_frequency(node,
				&scif_defterm_inclk);
	if (rc) {
		scif_defterm_skip_baud_config = TRUE;
	} else {
		scif_defterm_skip_baud_config = FALSE;
	}

	if (vmm_devtree_read_u32(node, "baudrate",
				&scif_defterm_baud)) {
		scif_defterm_baud = 115200;
	}

	if (vmm_devtree_getattr(node, "clock-internal")) {
		scif_defterm_use_intclk = TRUE;
	} else {
		scif_defterm_use_intclk = FALSE;
	}

	scif_lowlevel_init(scif_defterm_base,
			   scif_regtype,
			   scif_defterm_skip_baud_config,
			   scif_defterm_baud,
			   scif_defterm_inclk,
			   scif_defterm_use_intclk);

	return VMM_OK;
}

static int __init scifa_defterm_init(struct vmm_devtree_node *node) {
	scif_regtype = SCIx_SCIFA_REGTYPE;
	scif_defterm_init(node);

	return VMM_OK;
}

static struct defterm_ops scif_ops = {
	.putc = scif_defterm_putc,
	.getc = scif_defterm_getc,
	.init = scif_defterm_init
};

static struct defterm_ops scifa_ops = {
	.putc = scif_defterm_putc,
	.getc = scif_defterm_getc,
	.init = scifa_defterm_init
};

#else

#define scif_ops unknown_ops
#define scifa_ops unknown_ops

#endif

#if defined(CONFIG_SERIAL_BCM283X_MU)

#include <drv/serial/bcm283x_mu.h>

static virtual_addr_t bcm283x_mu_defterm_base;
static u32 bcm283x_mu_defterm_inclk;
static u32 bcm283x_mu_defterm_baud;
static bool bcm283x_mu_defterm_skip_baud_config;

static int bcm283x_mu_defterm_putc(u8 ch)
{
	if (!bcm283x_mu_lowlevel_can_putc(bcm283x_mu_defterm_base)) {
		return VMM_EFAIL;
	}
	bcm283x_mu_lowlevel_putc(bcm283x_mu_defterm_base, ch);
	return VMM_OK;
}

static int bcm283x_mu_defterm_getc(u8 *ch)
{
	if (!bcm283x_mu_lowlevel_can_getc(bcm283x_mu_defterm_base)) {
		return VMM_EFAIL;
	}
	*ch = bcm283x_mu_lowlevel_getc(bcm283x_mu_defterm_base);
	return VMM_OK;
}

static int __init bcm283x_mu_defterm_init(struct vmm_devtree_node *node)
{
	int rc;

	rc = vmm_devtree_regmap(node, &bcm283x_mu_defterm_base, 0);
	if (rc) {
		return rc;
	}

	rc = vmm_devtree_clock_frequency(node,
				&bcm283x_mu_defterm_inclk);
	if (rc) {
		bcm283x_mu_defterm_skip_baud_config = TRUE;
	} else {
		bcm283x_mu_defterm_skip_baud_config = FALSE;
	}

	if (vmm_devtree_read_u32(node, "baudrate",
				&bcm283x_mu_defterm_baud)) {
		bcm283x_mu_defterm_baud = 115200;
	}

	bcm283x_mu_lowlevel_init(bcm283x_mu_defterm_base,
				 bcm283x_mu_defterm_skip_baud_config,
				 bcm283x_mu_defterm_baud,
				 bcm283x_mu_defterm_inclk);

	return VMM_OK;
}

static struct defterm_ops bcm283x_mu_ops = {
	.putc = bcm283x_mu_defterm_putc,
	.getc = bcm283x_mu_defterm_getc,
	.init = bcm283x_mu_defterm_init
};

#else

#define bcm283x_mu_ops unknown_ops

#endif

#if defined(CONFIG_SERIAL_ZYNQ_UART)

#include <drv/serial/zynq-uart.h>

struct zynq_uart_priv uart_port;

static int zynq_uart_defterm_getc(u8 *ch)
{
	if (!zynq_uart_lowlevel_can_getc(uart_port.regs)) {
		return VMM_EFAIL;
	}
	*ch = zynq_uart_lowlevel_getc(uart_port.regs);
	return VMM_OK;
}

static int zynq_uart_defterm_putc(u8 ch)
{
	if (!zynq_uart_lowlevel_can_putc(uart_port.regs)) {
		return VMM_EFAIL;
	}

	zynq_uart_lowlevel_putc(uart_port.regs, ch);
	return VMM_OK;
}

static int __init zynq_uart_defterm_init(struct vmm_devtree_node *node)
{
	int rc;

	rc = vmm_devtree_regmap(node, (virtual_addr_t*)&uart_port.regs, 0);
	if (rc) {
		return rc;
	}
	rc = vmm_devtree_clock_frequency(node,
				&uart_port.input_clock);
	if (rc) {
		uart_port.skip_baudrate_config = TRUE;
	} else {
		uart_port.skip_baudrate_config = FALSE;
	}

	if (vmm_devtree_read_u32(node, "baudrate",
					&uart_port.baudrate)) {
		uart_port.baudrate = 115200;
	}

	zynq_uart_lowlevel_init(&uart_port);

	return VMM_OK;
}

static struct defterm_ops zynq_uart_ops = {
	.putc = zynq_uart_defterm_putc,
	.getc = zynq_uart_defterm_getc,
	.init = zynq_uart_defterm_init
};
#else

#define zynq_uart_ops unknown_ops

#endif

static struct vmm_devtree_nodeid defterm_devid_table[] = {
	{ .compatible = "arm,pl011", .data = &pl011_ops },
	{ .compatible = "ns8250", .data = &uart8250_ops },
	{ .compatible = "ns16450", .data = &uart8250_ops },
	{ .compatible = "ns16550a", .data = &uart8250_ops },
	{ .compatible = "ns16550", .data = &uart8250_ops },
	{ .compatible = "ns16750", .data = &uart8250_ops },
	{ .compatible = "ns16850", .data = &uart8250_ops },
	{ .compatible = "snps,dw-apb-uart", .data = &uart8250_ops },
	{ .compatible = "st16654", .data = &omapuart_ops },
	{ .compatible = "freescale", .data = &imx_ops },
	{ .compatible = "imx-uart", .data = &imx_ops },
	{ .compatible = "freescale,imx-uart", .data = &imx_ops },
	{ .compatible = "samsung", .data = &samsung_ops },
	{ .compatible = "exynos4210-uart", .data = &samsung_ops },
	{ .compatible = "samsung,exynos4210-uart", .data = &samsung_ops },
	{ .compatible = "renesas,scif", .data = &scif_ops },
	{ .compatible = "renesas,scifa", .data = &scifa_ops },
	{ .compatible = "brcm,bcm283x-mu", .data = &bcm283x_mu_ops },
	{ .compatible = "cdns,uart-r1p12", .data = &zynq_uart_ops },
	{ .compatible = "xlnx,xuartps", .data = &zynq_uart_ops },
	{ /* end of list */ },
};

static const struct defterm_ops *ops = &unknown_ops;

void defterm_set_initial_ops(struct defterm_ops *initial_ops)
{
	if (initial_ops) {
		ops = initial_ops;
	}
}

int arch_defterm_putc(u8 ch)
{
	return ops->putc(ch);
}

int arch_defterm_getc(u8 *ch)
{
	return ops->getc(ch);
}

int __init arch_defterm_init(void)
{
	int rc;
	const char *attr;
	struct vmm_devtree_node *node;
	const struct vmm_devtree_nodeid *nodeid;

	/* Find chosen node */
	node = vmm_devtree_getnode(VMM_DEVTREE_PATH_SEPARATOR_STRING
				   VMM_DEVTREE_CHOSEN_NODE_NAME);
	if (!node || !vmm_devtree_is_available(node)) {
		vmm_devtree_dref_node(node);
		return VMM_ENODEV;
	}

	/* Find console from chosen node */
	rc = vmm_devtree_read_string(node,
				VMM_DEVTREE_CONSOLE_ATTR_NAME, &attr);
	if (rc) {
		rc = vmm_devtree_read_string(node,
				VMM_DEVTREE_STDOUT_ATTR_NAME, &attr);
	}

	/* De-reference chosen node because we don't need it anymore. */
	vmm_devtree_dref_node(node);

	/* If we did not find console */
	if (rc) {
		/* Use initial defterm ops */
		return ops->init(NULL);
	}

	/* Find console node */
	node = vmm_devtree_getnode(attr);
	if (!node) {
		/* Use initial defterm ops */
		return ops->init(NULL);
	}

	/* Find matching defterm ops based on console node */
	nodeid = vmm_devtree_match_node(defterm_devid_table, node);
	if (nodeid) {
		ops = nodeid->data;
	}

	/* Use matching console defterm ops */
	rc = ops->init(node);
	vmm_devtree_dref_node(node);

	return rc;
}
