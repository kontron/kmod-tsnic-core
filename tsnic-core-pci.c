/* tsnic-core.c - core MFD driver for the Kontron TSN Network If Card
 * Copyright (C) 2017 Kontron. All rights reserved
 *
 * Contributors:
 *   Ralf Kihm
 *
 * Based on work found in cs5535-mfd.c .
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mfd/core.h>
#include <linux/if_ether.h>

#include "tsnic-core.h"

enum tsnic_mfd_bars {
	SWITCH_BAR = 0,
	TSE_BAR = 1,
	I2C_BAR = 5,
};

enum tsnic_mfd_irqs {
	DEIP_IRQ0 = 0,
	DEIP_IRQ1 = 1,
	TSE_IRQ0 = 2,
	TSE_IRQ1 = 3,
};

#define NR_BARS 3
#define MAX_RES 4

static void __iomem *bar5_virt = NULL;
static u8 eth_addr[ETH_ALEN];

static struct resource tsnic_mfd_resources[NR_BARS][MAX_RES];

static struct mfd_cell tsnic_mfd_cells[] = {
	{
		.id = TSE_BAR,
		.name = "tsnic-tse",
		.num_resources = 4,
		.resources = &tsnic_mfd_resources[TSE_BAR][0],
	},
	{
		.id = SWITCH_BAR,
		.name = "tsnic-deip",
		.num_resources = 4,
		.resources = &tsnic_mfd_resources[SWITCH_BAR][0],
	},
};

static void tsnic_setup_vpd(struct pci_dev *pdev)
{
	int err;

	err = tsnic_vpd_init(bar5_virt);
	if (err)
		dev_err(&pdev->dev, "vpd failed\n");
}

static int tsnic_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int err, i;
	struct resource *r;

	err = pci_enable_device(pdev);
	if (err)
		return err;

	pci_set_master(pdev);

	/* map bar 5 resource directly */
	bar5_virt = pci_iomap(pdev, I2C_BAR, 0);
	if (!bar5_virt)
		dev_err(&pdev->dev, "PCIe BAR 5 mapping failed!\n");

	tsnic_setup_vpd(pdev);

	if (tsnic_vpd_eth_hw_addr(eth_addr)) {
		get_random_bytes(eth_addr, ETH_ALEN);
		eth_addr[0] &= 0xfe;	/* clear multicast bit */
		eth_addr[0] |= 0x02;	/* set local assignment bit (IEEE802) */
	}

	dev_info(&pdev->dev, "%s eth address - %02x:%02x:%02x:%02x:%02x:%02x.\n",
			eth_addr[0] & 0x02 ? "Locally assigned" : "Burned-in",
			eth_addr[0], eth_addr[1], eth_addr[2],
			eth_addr[3], eth_addr[4], eth_addr[5]);

	/* fill in IO range for each cell; subdrivers handle the region */
	for (i = 0; i < ARRAY_SIZE(tsnic_mfd_cells); i++) {
		int bar = tsnic_mfd_cells[i].id;
		r = &tsnic_mfd_resources[bar][0];

		r->flags = IORESOURCE_MEM;
		r->start = pci_resource_start(pdev, bar);
		r->end = pci_resource_end(pdev, bar);

		/* eth addr */
		r = &tsnic_mfd_resources[bar][1];
		r->flags = IORESOURCE_REG;
		r->start = (unsigned long)&eth_addr[0];
		r->end = (unsigned long)&eth_addr[6];

		/* id is used for temporarily storing BAR; unset it now */
		tsnic_mfd_cells[i].id = 0;
	}

	if (pci_alloc_irq_vectors(pdev, 1, 32, PCI_IRQ_MSIX) != 4) {
		// TODO: fall back to MSI or legacy interrupts
		goto err_disable;
	}

	r = &tsnic_mfd_resources[0][2];
	r->flags = IORESOURCE_IRQ;
	r->start = r->end = pci_irq_vector(pdev, DEIP_IRQ0);

	r = &tsnic_mfd_resources[0][3];
	r->flags = IORESOURCE_IRQ;
	r->start = r->end = pci_irq_vector(pdev, DEIP_IRQ1);

	r = &tsnic_mfd_resources[1][2];
	r->flags = IORESOURCE_IRQ;
	r->start = r->end = pci_irq_vector(pdev, TSE_IRQ1);

	r = &tsnic_mfd_resources[1][3];
	r->flags = IORESOURCE_IRQ;
	r->start = r->end = pci_irq_vector(pdev, TSE_IRQ0);

	err = mfd_add_devices(&pdev->dev, -1, tsnic_mfd_cells,
			      ARRAY_SIZE(tsnic_mfd_cells), NULL, 0, NULL);
	if (err) {
		dev_err(&pdev->dev, "MFD add devices failed: %d\n", err);
		goto err_disable;
	}

	dev_info(&pdev->dev, "%zu devices registered.\n",
			ARRAY_SIZE(tsnic_mfd_cells));

	return 0;

err_disable:
	pci_disable_device(pdev);
	return err;
};

static void tsnic_pci_remove(struct pci_dev *pdev)
{
	if (bar5_virt)
		pci_iounmap(pdev, bar5_virt);

	mfd_remove_devices(&pdev->dev);
	pci_free_irq_vectors(pdev);
	pci_disable_device(pdev);
	return;
};

static const struct pci_device_id tsnic_pci_tbl[] = {
	{PCI_DEVICE(0x1059, 0xa100), .driver_data = 0},
	{0, }
};

MODULE_DEVICE_TABLE(pci, tsnic_pci_tbl);

static struct pci_driver tsnic_pci_driver = {
	.name     = "tsnic-core",
	.id_table = tsnic_pci_tbl,
	.probe    = tsnic_pci_probe,
	.remove   = tsnic_pci_remove,
};

static int __init tsnic_init_module(void)
{
	int rv = -ENODEV;

#ifdef CONFIG_PCI
	rv = pci_register_driver(&tsnic_pci_driver);
	if (rv)
		pr_err("Unable to register PCI driver: %d\n", rv);
#endif

	return rv;
}

module_init(tsnic_init_module);


static void tsnic_exit_module(void)
{
#ifdef CONFIG_PCI
	pci_unregister_driver(&tsnic_pci_driver);
#endif
}

module_exit(tsnic_exit_module);

MODULE_AUTHOR("Kontron");
MODULE_DESCRIPTION("tsnic core mfd driver");
MODULE_LICENSE("GPL v2");
