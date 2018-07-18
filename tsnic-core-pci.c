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
#include <linux/property.h>

#include "tsnic-core.h"

#define FPGA_MIN_VEERSION 0x10

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
static char asset[TSNIC_SNO_LEN + 1];

static struct resource tsnic_tse_resources[] = {
	{
		.start = 0,
		.end = 0xfff,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = TSE_IRQ1,
		.end = TSE_IRQ1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = TSE_IRQ0,
		.end = TSE_IRQ0,
		.flags = IORESOURCE_IRQ,
	},
};

/* deipce */
static struct resource deipce_resources[] = {
	{
		.start = 0x3000000,
		.end =   0x30fffff,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = DEIP_IRQ0,
		.end = DEIP_IRQ0,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = 0x0000000,
		.end =   0x000ffff,
		.flags = IORESOURCE_MEM,
	},
};

/* deipce-mdio */
static struct resource deipce_mdio_resources[] = {
	{
		.start = 0x4000000,
		.end = 0x40000ff,
		.flags = IORESOURCE_MEM,
	},
};

/* flx_ac */
static struct resource flx_ac_resources[] = {
	{
		.start = 0x2000000,
		.end = 0x200ffff,
		.flags = IORESOURCE_MEM,
	},
};

/* flx_ibc */
static struct resource flx_ibc_resources[] = {
	{
		.start = 0x2100000,
		.end = 0x210ffff,
		.flags = IORESOURCE_MEM,
	},
};

/* flx_fpts */
static struct resource flx_fpts0_resources[] = {
	{
		.start = 0x2120000,
		.end = 0x212ffff,
		.flags = IORESOURCE_MEM,
	},
};

/* flx_frtc */
static struct resource flx_frtc0_resources[] = {
	{
		.start = 0x2180000,
		.end = 0x218ffff,
		.flags = IORESOURCE_MEM,
	},
};
static struct resource flx_frtc1_resources[] = {
	{
		.start = 0x2190000,
		.end = 0x219ffff,
		.flags = IORESOURCE_MEM,
	},
};

/* flx_fsc */
static struct resource flx_fsc_resources[] = {
	{
		.start = 0x21c0000,
		.end = 0x21cffff,
		.flags = IORESOURCE_MEM,
	},
};

static struct property_entry generic_properties[] = {
		PROPERTY_ENTRY_U8_ARRAY("mac-address", eth_addr),
		{}
};

static struct mfd_cell tsnic_bar0[] = {
	{
		.id = 1,
		.name = "deipce",
		.num_resources = ARRAY_SIZE(deipce_resources),
		.resources = deipce_resources,
		.properties = generic_properties,
	},
	{
		.name = "flx_ac",
		.id = 1,
		.num_resources = ARRAY_SIZE(flx_ac_resources),
		.resources = flx_ac_resources,
	},
	{
		.name = "flx_ibc",
		.id = 1,
		.num_resources = ARRAY_SIZE(flx_ibc_resources),
		.resources = flx_ibc_resources,
	},
	{
		.name = "flx_fpts",
		.id = 1,
		.num_resources = ARRAY_SIZE(flx_fpts0_resources),
		.resources = flx_fpts0_resources,
	},
	{
		.name = "flx_frtc",
		.id = 1,
		.num_resources = ARRAY_SIZE(flx_frtc0_resources),
		.resources = flx_frtc0_resources,
	},
	{
		.name = "flx_frtc",
		.id = 2,
		.num_resources = ARRAY_SIZE(flx_frtc1_resources),
		.resources = flx_frtc1_resources,
	},
	{
		.id = 1,
		.name = "flx_fsc",
		.num_resources = ARRAY_SIZE(flx_fsc_resources),
		.resources = flx_fsc_resources,
	},
	{
		.id = 1,
		.name = "deipce-mdio",
		.num_resources = ARRAY_SIZE(deipce_mdio_resources),
		.resources = deipce_mdio_resources,
	},
};

static struct mfd_cell tsnic_bar1[] = {
	{
		.name = "tsnic-tse",
		.num_resources = ARRAY_SIZE(tsnic_tse_resources),
		.resources = tsnic_tse_resources,
		.properties = generic_properties,
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
	int err;
	int irqbase;
	u16 version;

	err = pci_enable_device(pdev);
	if (err)
		return err;

	pci_set_master(pdev);

	pci_read_config_word(pdev, PCI_CLASS_REVISION, &version);
	version &= 0x7f;

	if (version < FPGA_MIN_VEERSION) {
		err = -ENODEV;
		dev_err(&pdev->dev,
			"fpga version 0x%02x detected, but min. 0x%02x required!\n",
			version, FPGA_MIN_VEERSION);
		goto err_disable;
	}

	dev_info(&pdev->dev, "fpga version %d(%02xh) found.\n", version, version);

	/* map bar 5 resource directly */
	bar5_virt = pci_iomap(pdev, I2C_BAR, 0);
	if (!bar5_virt) {
		dev_err(&pdev->dev, "PCIe BAR 5 mapping failed!\n");
		err = -ENODEV;
		goto err_disable;
	}

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

	(void) tsnic_vpd_asset_tag(asset, sizeof(asset));

	dev_info(&pdev->dev, "Serial number - %s.\n", asset);

	if (pci_alloc_irq_vectors(pdev, 1, 32, PCI_IRQ_MSIX) != 4) {
		// TODO: fall back to MSI or legacy interrupts
		err = -ENODEV;
		goto err_disable;
	}
	irqbase = pci_irq_vector(pdev, 0);

	err = mfd_add_devices(&pdev->dev, -1, tsnic_bar0,
			      ARRAY_SIZE(tsnic_bar0), &pdev->resource[0], irqbase, NULL);
	if (err) {
		dev_err(&pdev->dev, "MFD add devices failed: %d\n", err);
		goto err_free;
	}

	err = mfd_add_devices(&pdev->dev, -1, tsnic_bar1,
			      ARRAY_SIZE(tsnic_bar1), &pdev->resource[1], irqbase, NULL);
	if (err) {
		dev_err(&pdev->dev, "MFD add devices failed: %d\n", err);
		goto err_remove_devices;
	}

	dev_info(&pdev->dev, "%zu devices registered.\n",
			ARRAY_SIZE(tsnic_bar0) + ARRAY_SIZE(tsnic_bar1));

	return 0;

err_remove_devices:
	mfd_remove_devices(&pdev->dev);

err_free:
	pci_free_irq_vectors(pdev);

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
