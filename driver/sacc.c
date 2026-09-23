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

struct sacc_dma {
	void *sq_addr;
	dma_addr_t sq_dma_addr;	
	size_t sq_size;
	size_t sq_depth;
	dma_addr_t cq_dma_addr;	
	void *cq_addr;
	size_t cq_size;
	size_t cq_depth;
} sdma;

#define SACC_SQ_SIZE 32 // per spec
#define SACC_SQ_DEPTH 64 // arbitrary
#define SACC_CQ_SIZE 32 // per spec
#define SACC_CQ_DEPTH 64 // arbitrary

static int sacc_init_dma(struct pci_dev *pdev, struct sacc_dma *sdma) {

	sdma->sq_size = SACC_SQ_SIZE;
	sdma->sq_depth = SACC_SQ_DEPTH;
	sdma->cq_size = SACC_CQ_SIZE;
	sdma->cq_depth = SACC_CQ_DEPTH;

	sdma->sq_addr = dma_alloc_coherent(&pdev->dev, sdma->sq_size * sdma->sq_depth,
			                   &sdma->sq_dma_addr, GFP_KERNEL);
	if (!sdma->sq_addr) 
		return -ENOMEM;

	dev_info(&pdev->dev, 
		 "DMA coherent reserved for submission queues, %ld bytes...\n", 
		 sdma->sq_size * sdma->sq_depth);

	sdma->cq_addr = dma_alloc_coherent(&pdev->dev, sdma->cq_size * sdma->cq_depth,
			                   &sdma->cq_dma_addr, GFP_KERNEL);
	if (!sdma->cq_addr) 
		return -ENOMEM;

	dev_info(&pdev->dev, 
		 "DMA coherent reserved for completion queues, %ld bytes...\n", 
		 sdma->cq_size * sdma->cq_depth);


	return 0;
}

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

	sacc_init_dma(pdev, &sdma);

	/* Posted writes do not wait on device confirmation */
	iowrite32(lower_32_bits(sdma.sq_dma_addr), mmio_base + SACC_REG_SQ_BASE_LO);
	iowrite32(upper_32_bits(sdma.sq_dma_addr), mmio_base + SACC_REG_SQ_BASE_HI);
	iowrite32(sdma.sq_depth, mmio_base + SACC_REG_SQ_SIZE);

	iowrite32(lower_32_bits(sdma.cq_dma_addr), mmio_base + SACC_REG_CQ_BASE_LO);
	iowrite32(upper_32_bits(sdma.cq_dma_addr), mmio_base + SACC_REG_CQ_BASE_HI);
	iowrite32(sdma.cq_depth, mmio_base + SACC_REG_CQ_SIZE);

	dev_info(&pdev->dev, "SQ addr=%08x:%08x size=%u\n", 
			ioread32(mmio_base + SACC_REG_SQ_BASE_HI),
			ioread32(mmio_base + SACC_REG_SQ_BASE_LO),
			ioread32(mmio_base + SACC_REG_SQ_SIZE));

	dev_info(&pdev->dev, "CQ addr=%08x:%08x size=%u\n", 
			ioread32(mmio_base + SACC_REG_CQ_BASE_HI),
			ioread32(mmio_base + SACC_REG_CQ_BASE_LO),
			ioread32(mmio_base + SACC_REG_CQ_SIZE));

	pci_set_drvdata(pdev, mmio_base);

	/* insert ioremap/IRQ reg */
	return 0;
}


static int sacc_free_dma(struct pci_dev *pdev, struct sacc_dma *sdma) {

	sdma->sq_size = SACC_SQ_SIZE;
	sdma->sq_depth = SACC_SQ_DEPTH;
	sdma->cq_size = SACC_CQ_SIZE;
	sdma->cq_depth = SACC_CQ_DEPTH;

	dma_free_coherent(&pdev->dev, sdma->sq_size * sdma->sq_depth, sdma->sq_addr, 
			  sdma->sq_dma_addr);
	dev_info(&pdev->dev, 
		 "DMA coherent freed for submission queues, %ld bytes...\n", 
		 sdma->cq_size * sdma->cq_depth);

	dma_free_coherent(&pdev->dev, sdma->cq_size * sdma->cq_depth, sdma->cq_addr, 
			  sdma->cq_dma_addr);
	dev_info(&pdev->dev, 
		 "DMA coherent freed for completion queues, %ld bytes...\n", 
		 sdma->cq_size * sdma->cq_depth);

	if (!sdma->cq_addr) 
		return -ENOMEM;

	return 0;
}

static void sacc_pci_remove(struct pci_dev *pdev) 
{
	sacc_free_dma(pdev, &sdma);
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
