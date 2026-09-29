/*
 * SACC: Synthetic PCIe Accelerator
 *
 * Copyright (C) Linear Group LLC, 2026
 * Author: John Rose, john@lineargp.com
 * 
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/misc/sacc_regs.h"
#include "trace.h"

#define TYPE_PCI_SACC_DEVICE "sacc"
#define SACC_BAR_SIZE 8192
#define SACC_PAGE_SIZE 4096 

#define SACC_PCIE_CAP_OFFSET 0x40

struct SaccState {
    PCIDevice pdev;
    MemoryRegion bar0;
    MemoryRegion mmio_regs;
    MemoryRegion mmio_db;
    uint32_t magic;
    uint32_t version;
    uint32_t ctrl;
    uint32_t status;
    uint64_t sq_base;
    uint32_t sq_size;
    uint32_t sq_head;
    uint32_t sq_tail;
    uint64_t cq_base;
    uint32_t cq_size;
    uint32_t cq_head;
    uint32_t cq_tail;
    uint16_t cq_phase;
    uint32_t irq_status;
    uint32_t irq_ack;
    uint32_t err_code;
    uint32_t value;
};

typedef struct SaccState SaccState;
DECLARE_INSTANCE_CHECKER(SaccState, SACC, TYPE_PCI_SACC_DEVICE);

static uint64_t sacc_mmio_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    SaccState *s = opaque;
    uint64_t val;

    switch(addr) {
        case SACC_REG_MAGIC:
            val = s->magic;
            break;
        case SACC_REG_VERSION:
            val = s->version;
            break;
        case SACC_REG_CTRL:
            val = s->ctrl;
            break;
        case SACC_REG_STATUS:
            val = s->status;
            break;
        case SACC_REG_SQ_BASE_LO:
            val = (uint32_t) s->sq_base;
            break;
        case SACC_REG_SQ_BASE_HI:
            val = s->sq_base >> 32;
            break;
        case SACC_REG_SQ_SIZE:
            val = s->sq_size;
            break;
        case SACC_REG_CQ_BASE_LO:
            val = (uint32_t) s->cq_base;
            break;
        case SACC_REG_CQ_BASE_HI:
            val = s->cq_base >> 32;
            break;
        case SACC_REG_CQ_SIZE:
            val = s->cq_size;
            break;
        case SACC_REG_CQ_HEAD:
            return 0;
            break;
        case SACC_REG_IRQ_STATUS:
            val = s->irq_status;
            break;
        case SACC_REG_IRQ_ACK:
            val = 0;
            break;
        case SACC_REG_ERR_CODE:
            val = s->err_code;
            break;
        default:
            val = 0;
            break;
    }
    trace_sacc_mmio_reg_read(addr, val, size);

    return val;
}

static void sacc_mmio_reg_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
    SaccState *s = opaque;

    trace_sacc_mmio_reg_write(addr, val, size);

    switch(addr) {
	    case SACC_REG_CTRL:
	        s->ctrl = (uint32_t) val;
	        break;
	    case SACC_REG_SQ_BASE_LO:
	        s->sq_base = (s->sq_base & 0xffffffff00000000ULL) | (uint32_t) val;
	        break;
	    case SACC_REG_SQ_BASE_HI:
		    s->sq_base = (s->sq_base & 0x00000000ffffffffULL) | \
                         ((uint64_t)(uint32_t)val << 32);
		    break;
	    case SACC_REG_SQ_SIZE:
	        s->sq_size = (uint32_t) val;
	        break;
	    case SACC_REG_CQ_BASE_LO:
	        s->cq_base = (s->cq_base & 0xffffffff00000000ULL) | (uint32_t) val;
	        break;
	    case SACC_REG_CQ_BASE_HI:
		    s->cq_base = (s->cq_base & 0x00000000ffffffffULL) | \
                         ((uint64_t)(uint32_t)val << 32);
		    break;
	    case SACC_REG_CQ_SIZE:
	        s->cq_size = (uint32_t) val;
	        break;
	    case SACC_REG_CQ_HEAD:
	        s->cq_head = (uint32_t) val;
	        break;
        default:
            break;
    }
}

static const MemoryRegionOps sacc_mmio_reg_ops = {
    .read = sacc_mmio_reg_read,
    .write = sacc_mmio_reg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 8 },
    .impl = { .min_access_size = 4, .max_access_size = 4 },
};

static int sacc_cq_post(SaccState *s, uint32_t req_id, uint16_t status,
		uint32_t result_len)
{
    struct sacc_cq_entry ce = {
        .req_id     = cpu_to_le32(req_id),
        .result_len = cpu_to_le32(result_len),
        .status     = cpu_to_le16(status),
    };
    dma_addr_t a = s->cq_base + (dma_addr_t)s->cq_tail * sizeof(ce);
    uint16_t phase = cpu_to_le16(s->cq_phase);

    if ((s->cq_tail + 1) % s->cq_size == s->cq_head) {
        trace_sacc_cq_full(s->cq_tail, s->cq_head);
        return 1;                      /* stall */
    }

    /* 1) write everything except phase (first 14 of 16 bytes) */
    if (pci_dma_write(&s->pdev, a, &ce, offsetof(struct sacc_cq_entry, phase)) != MEMTX_OK ||
    /* 2) then write phase (last 2 of 16 bytes), publishing the entry for slot */
        pci_dma_write(&s->pdev, a + offsetof(struct sacc_cq_entry, phase),
                      &phase, sizeof(phase)) != MEMTX_OK) {
        trace_sacc_cq_post_err(s->cq_tail, a);
        return 1;
    }

    /* s->cq->tail is the slot number, phase is 1 upon init */
    trace_sacc_cq_post(s->cq_tail, req_id, status, result_len, s->cq_phase);

    if (++s->cq_tail == s->cq_size) {
        s->cq_tail = 0;
        s->cq_phase ^= 1;                  /* flip on wrap */
    }

    return 0;
}
	
static uint64_t sacc_mmio_db_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void sacc_mmio_db_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
    SaccState *s = opaque;
    struct sacc_sq_desc sd;
    uint64_t dma_addr;
    uint32_t req_id;
    uint32_t len;
    uint16_t op;
    uint16_t status;

    if ((addr != 0) || (s->sq_size == 0)) {
            return;
    }
     
    s->sq_tail = val % s->sq_size;
    trace_sacc_doorbell(s->sq_tail);

    /* refactor consumption loop soon */
    while (s->sq_head != s->sq_tail) {
	    struct sacc_sq_desc desc;
	    dma_addr = s->sq_base + ((dma_addr_t)s->sq_head * sizeof(desc));

	    /* fetch */
	    if (pci_dma_read(&s->pdev, dma_addr, &sd, sizeof(sd)) != MEMTX_OK) {
		    trace_sacc_sq_fetch_err(s->sq_head, dma_addr);	
	    }
	        req_id = le32_to_cpu(sd.req_id);
	        len = le32_to_cpu(sd.len);
		    op = le16_to_cpu(sd.opcode);

	        trace_sacc_sq_fetch(s->sq_head, le64_to_cpu(sd.src_addr),
	    	    le64_to_cpu(sd.dst_addr), le32_to_cpu(sd.len),
	    	    op, le16_to_cpu(sd.flags),
	    	    req_id);

            /* execute sq work */
	    status = SACC_ERR_OK;
	    trace_sacc_exec(req_id, op, len);
	    switch (op) {
		    case SACC_OP_NOP:
			    break;
		    default:
		            status = SACC_ERR_BAD_OPCODE;
	    		    trace_sacc_exec_err(req_id, status);
			    break;
	    }

	    /* post completion */
	    if (sacc_cq_post(s, req_id, status, 0) != 0) {
		    return; 	// CQ FULL
	    }

	    /* desc finished */
    	    s->sq_head = (s->sq_head + 1) % s->sq_size;
    }
}

static const MemoryRegionOps sacc_mmio_db_ops = {
    .read = sacc_mmio_db_read,
    .write = sacc_mmio_db_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl = { .min_access_size = 4, .max_access_size = 4 },
};

static void pci_sacc_realize(PCIDevice *pdev, Error **errp)
{
    SaccState *s = SACC(pdev);
/*    uint8_t *pci_conf = pdev->config; */

    s->magic = SACC_MAGIC_VAL;
    s->version = SACC_VERS_VAL;
    s->ctrl = 0;
    s->status = 0;
    s->sq_base = 0ll;
    s->sq_size = 0;
    s->cq_base = 0ll;
    s->cq_size = 0;
    s->cq_head = 0;
    s->cq_phase = 1;
    s->irq_status = 0;
    s->irq_ack = 0;
    s->err_code = 0;

    memory_region_init(&s->bar0, OBJECT(s), "sacc-bar0", SACC_BAR_SIZE);
    memory_region_init_io(&s->mmio_regs, OBJECT(s), &sacc_mmio_reg_ops, s, \
            "sacc-regs", SACC_PAGE_SIZE);
    memory_region_init_io(&s->mmio_db, OBJECT(s), &sacc_mmio_db_ops, s, \
            "sacc-doorbell", SACC_PAGE_SIZE);

    memory_region_add_subregion(&s->bar0, 0x0000, &s->mmio_regs);
    memory_region_add_subregion(&s->bar0, 0x1000, &s->mmio_db);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);
    pcie_endpoint_cap_init(pdev, SACC_PCIE_CAP_OFFSET);
}

static void sacc_class_init(ObjectClass *class, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_sacc_realize;
    k->vendor_id = SACC_VENDOR_ID;
    k->device_id = SACC_DEVICE_ID;
    k->revision = 0x01;
    k->class_id = 0x1200;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo sacc_types[] = {
    {
        .name          = TYPE_PCI_SACC_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(SaccState),
        .class_init    = sacc_class_init,
        .interfaces    = (const InterfaceInfo[]) {
            { INTERFACE_PCIE_DEVICE },
            { },
        },
    }
};

DEFINE_TYPES(sacc_types)
