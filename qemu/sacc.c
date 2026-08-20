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
    uint64_t cq_base;
    uint32_t cq_size;
    uint32_t cq_head;
    uint32_t irq_status;
    uint32_t irq_ack;
    uint32_t err_code;

    uint32_t sq_tail;
    uint32_t value;
};

typedef struct SaccState SaccState;
DECLARE_INSTANCE_CHECKER(SaccState, SACC, TYPE_PCI_SACC_DEVICE);

static uint64_t sacc_mmio_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    SaccState *s = opaque;

    switch(addr) {
        case SACC_REG_MAGIC:
            return s->magic;
        case SACC_REG_VERSION:
            return s->version;
        case SACC_REG_CTRL:
            return s->ctrl;
        case SACC_REG_STATUS:
            return s->status;
        case SACC_REG_SQ_BASE_LO:
            return (uint32_t) s->sq_base;
        case SACC_REG_SQ_BASE_HI:
            return s->sq_base >> 32;
        case SACC_REG_SQ_SIZE:
            return s->sq_size;
        case SACC_REG_CQ_BASE_LO:
            return (uint32_t) s->cq_base;
        case SACC_REG_CQ_BASE_HI:
            return s->cq_base >> 32;
        case SACC_REG_CQ_SIZE:
            return s->cq_size;
        case SACC_REG_CQ_HEAD:
            return 0;
        case SACC_REG_IRQ_STATUS:
            return s->irq_status;
        case SACC_REG_IRQ_ACK:
            return 0;
        case SACC_REG_ERR_CODE:
            return s->err_code;
        default:
            return 0;
    }
}

static void sacc_mmio_reg_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
/*
    SaccState *s = opaque;
    switch(addr) {
        case 0x04:
            s->value = val;
            break;
    }
    */
    ;

}

static const MemoryRegionOps sacc_mmio_reg_ops = {
    .read = sacc_mmio_reg_read,
    .write = sacc_mmio_reg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 8 },
    .impl = { .min_access_size = 4, .max_access_size = 4 },
};

static uint64_t sacc_mmio_db_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void sacc_mmio_db_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
    SaccState *s = opaque;

    if (addr != 0) {
            return;
    }
     
    s->sq_tail = val;
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
