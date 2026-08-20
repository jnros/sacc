// SPDX-License-Identifier: GPL-2.0
/*
 * Synethetic PCIe Accelerator, Control Plane
 *
 * Copyright (C) 2026 Linear Group LLC
 * Author: John Rose, john@lineargp.com
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/vmalloc.h>
#include "../sacc_regs.h"

static const struct pci_device_id sacc_pci_ids[] = {
	{ PCI_DEVICE(SACC_VENDOR_ID, SACC_DEVICE_ID) } ,
			{ 0, } 
};
MODULE_DEVICE_TABLE(pci, sacc_pci_ids);

static int sacc_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int rc;
	void __iomem *mmio_base;
	u32 magic;

	dev_info(&pdev->dev, "sacc: probe detected, initializing...\n");

	rc = pci_enable_device(pdev);
	if (rc) return rc;

	rc = pci_request_region(pdev, 0, "sacc");
	if (rc) {
		dev_err(&pdev->dev, "failed to request region\n");
		pci_disable_device(pdev);
		return rc;
	}

	mmio_base = pci_iomap(pdev, 0, 4096);
	if (!mmio_base) {
		dev_err(&pdev->dev, "failed to map MMIO BAR\n");
		return -EIO;
	}

	magic = ioread32(mmio_base + SACC_REG_MAGIC);

	if (magic != SACC_MAGIC_VAL) {
		dev_err(&pdev->dev, "bad magic num, expected 0x%08X got 0x%08X\n",
				SACC_MAGIC_VAL, magic);
		return -ENODEV;
	}

	dev_info(&pdev->dev, "magic BAR validated 0x%08X, initializing...\n", magic);

	pci_set_drvdata(pdev, mmio_base);

	/* insert ioremap/IRQ reg */
	return 0;
}


static void sacc_pci_remove(struct pci_dev *pdev) 
{
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	dev_info(&pdev->dev, "pci device removed");
}

static struct pci_driver sacc_driver = {
	.name = "sacc",
	.id_table = sacc_pci_ids,
	.probe = sacc_pci_probe,
	.remove = sacc_pci_remove,

};

module_pci_driver(sacc_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Rose <john@lineargp.com>");
MODULE_DESCRIPTION("SACC PCI Driver");
