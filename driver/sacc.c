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
#include <linux/delay.h>
#include "../sacc_regs.h"

static const struct pci_device_id sacc_pci_ids[] = {
	{ PCI_DEVICE(SACC_VENDOR_ID, SACC_DEVICE_ID) } ,
			{ 0, } 
};

MODULE_DEVICE_TABLE(pci, sacc_pci_ids);

struct sacc_dma {
	struct sacc_sq_desc *sq;
	dma_addr_t sq_dma_addr;	
	size_t sq_size;
	size_t sq_depth;
	uint32_t sq_tail;
	struct sacc_cq_entry *cq;
	dma_addr_t cq_dma_addr;	
	uint32_t cq_head;
	size_t cq_size;
	size_t cq_depth;
	uint16_t cq_phase;
} sdma;

#define SACC_SQ_SIZE 32 // per spec
#define SACC_SQ_DEPTH 64 // arbitrary
#define SACC_CQ_SIZE 16 // per spec
#define SACC_CQ_DEPTH 64 // arbitrary

static inline int sacc_cq_ready(struct sacc_cq_entry *ce, u16 phase)
{
	return (le16_to_cpu(READ_ONCE(ce->phase)) & 1) == phase;
}

#define CQ_POLL_TRIES 1000

static int sacc_poll_cq(struct pci_dev *pdev, struct sacc_dma *sdma,
		void __iomem *mmio_base) 
{
	struct sacc_cq_entry *ce = &sdma->cq[sdma->cq_head];
	int tries = CQ_POLL_TRIES;
	int ready = 0;

	while (!ready && (tries > 0)) {
		ready = sacc_cq_ready(ce, sdma->cq_phase);
		udelay(10);
		tries = tries - 1;
	}

	if (!ready) {
		dev_err(&pdev->dev, "CQ poll timeout at head %u\n",
				sdma->cq_head);
		return -ETIMEDOUT;
	}

	dma_rmb();

	dev_info(&pdev->dev, "completion: req_id = 0x%x status 0x%x len=%u\n",
			le32_to_cpu(ce->req_id), le16_to_cpu(ce->status),
			le32_to_cpu(ce->result_len));

	sdma->cq_head = (sdma->cq_head + 1);
	if (sdma->cq_head == sdma->cq_depth) {
		sdma->cq_head = 0;
		sdma->cq_phase ^= 1;  	//flip on wrap
	}

	iowrite32(sdma->cq_head, mmio_base + SACC_REG_CQ_HEAD);

	return 0;
}


static int sacc_doorbell_init(struct pci_dev *pdev, struct sacc_dma *sdma, 
	                      void * mmdb_base) {
	struct sacc_sq_desc *sdesc = &sdma->sq[sdma->sq_tail];

	/* Submisison step 1: Populate struct in host mem */
	if (sizeof(*sdesc) != SACC_SQ_SIZE) {
		dev_info(&pdev->dev,
			 "sq_desc size mismatch %ld != %d\n", sizeof(sdesc), 
			 SACC_SQ_SIZE);
		return 1;
	}

	memset(sdesc, 0, sizeof(struct sacc_sq_desc));
	sdesc->req_id = 0x0001;
	sdesc->opcode = SACC_OP_NOP;

	/* incr ring buf tail w wrap */
	sdma->sq_tail = (sdma->sq_tail + 1) % sdma->sq_depth; 

	/* Submission step 2: ring the doorbell on device side */
	iowrite32(sdma->sq_tail, mmdb_base);

	return 0;
}

static int sacc_init_dma(struct pci_dev *pdev, struct sacc_dma *sdma) {

	sdma->sq_size = SACC_SQ_SIZE;
	sdma->sq_depth = SACC_SQ_DEPTH;
	sdma->cq_size = SACC_CQ_SIZE;
	sdma->cq_depth = SACC_CQ_DEPTH;
	sdma->sq_tail = 0;
	sdma->cq_head = 0;
	sdma->cq_phase = 1;

	sdma->sq = dma_alloc_coherent(&pdev->dev, sdma->sq_size * sdma->sq_depth,
			                   &sdma->sq_dma_addr, GFP_KERNEL);
	if (!sdma->sq) 
		return -ENOMEM;

	dev_info(&pdev->dev, 
		 "DMA coherent reserved for submission queues, %ld bytes...\n", 
		 sdma->sq_size * sdma->sq_depth);

	sdma->cq = dma_alloc_coherent(&pdev->dev, sdma->cq_size * sdma->cq_depth,
			                   &sdma->cq_dma_addr, GFP_KERNEL);
	if (!sdma->cq) 
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
	void __iomem *mmdb_base;
	u32 magic;

	dev_info(&pdev->dev, "sacc: probe detected, initializing...\n");

	rc = pci_enable_device(pdev);
	if (rc) return rc;

	pci_set_master(pdev);
	rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (rc) {
		dev_err(&pdev->dev, "failed to set dma mask\n");
		return rc;
	}

	rc = pci_request_region(pdev, 0, "sacc");
	if (rc) {
		dev_err(&pdev->dev, "failed to request region\n");
		pci_disable_device(pdev);
		return rc;
	}

	mmio_base = pci_iomap(pdev, 0, 8192);
	if (!mmio_base) {
		dev_err(&pdev->dev, "failed to map MMIO BAR\n");
		return -EIO;
	}

	/* separately map doorbell page */
	mmdb_base = mmio_base + 4096;
	if (!mmdb_base) {
		dev_err(&pdev->dev, "failed to map doorbell page\n");
		return -EIO;
	}

	magic = ioread32(mmio_base + SACC_REG_MAGIC);
	if (magic != SACC_MAGIC_VAL) {
		dev_err(&pdev->dev, "bad magic num, expected 0x%08X got 0x%08X\n",
				SACC_MAGIC_VAL, magic);
		return -ENODEV;
	}

	dev_info(&pdev->dev, "magic BAR validated 0x%08X, initializing...\n", magic);

	rc = sacc_init_dma(pdev, &sdma);
	if (rc) 
		return rc;

	/* Posted writes do not wait on device confirmation */
	iowrite32(lower_32_bits(sdma.sq_dma_addr), mmio_base + SACC_REG_SQ_BASE_LO);
	iowrite32(upper_32_bits(sdma.sq_dma_addr), mmio_base + SACC_REG_SQ_BASE_HI);
	iowrite32(sdma.sq_depth, mmio_base + SACC_REG_SQ_SIZE);

	iowrite32(lower_32_bits(sdma.cq_dma_addr), mmio_base + SACC_REG_CQ_BASE_LO);
	iowrite32(upper_32_bits(sdma.cq_dma_addr), mmio_base + SACC_REG_CQ_BASE_HI);
	iowrite32(sdma.cq_depth, mmio_base + SACC_REG_CQ_SIZE);

	/* PCIe reads are non-posted, flush prior writes */
	dev_info(&pdev->dev, "SQ addr=%08x:%08x size=%u\n", 
			ioread32(mmio_base + SACC_REG_SQ_BASE_HI),
			ioread32(mmio_base + SACC_REG_SQ_BASE_LO),
			ioread32(mmio_base + SACC_REG_SQ_SIZE));

	dev_info(&pdev->dev, "CQ addr=%08x:%08x size=%u\n", 
			ioread32(mmio_base + SACC_REG_CQ_BASE_HI),
			ioread32(mmio_base + SACC_REG_CQ_BASE_LO),
			ioread32(mmio_base + SACC_REG_CQ_SIZE));

	pci_set_drvdata(pdev, mmio_base);

	sacc_doorbell_init(pdev, &sdma, mmdb_base);

	rc = sacc_poll_cq(pdev, &sdma, mmio_base);
	if (rc)
		return rc;

	/* insert IRQ reg */
	return 0;
}

static int sacc_free_dma(struct pci_dev *pdev, struct sacc_dma *sdma) {

	sdma->sq_size = SACC_SQ_SIZE;
	sdma->sq_depth = SACC_SQ_DEPTH;
	sdma->cq_size = SACC_CQ_SIZE;
	sdma->cq_depth = SACC_CQ_DEPTH;

	dma_free_coherent(&pdev->dev, sdma->sq_size * sdma->sq_depth, sdma->sq, 
			  sdma->sq_dma_addr);
	dev_info(&pdev->dev, 
		 "DMA coherent freed for submission queues, %ld bytes...\n", 
		 sdma->cq_size * sdma->cq_depth);

	dma_free_coherent(&pdev->dev, sdma->cq_size * sdma->cq_depth, sdma->cq, 
			  sdma->cq_dma_addr);
	dev_info(&pdev->dev, 
		 "DMA coherent freed for completion queues, %ld bytes...\n", 
		 sdma->sq_size * sdma->sq_depth);

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
