#ifndef SACC_REGS_H
#define SACC_REGS_H

/*
 * SACC: Synthetic PCIe Accelerator Control Plane
 */

#define SACC_VENDOR_ID          0x1B36
#define SACC_DEVICE_ID          0x1092

#define SACC_REG_MAGIC          0x00
#define SACC_REG_VERSION        0x04
#define SACC_REG_CTRL           0x08
#define SACC_REG_STATUS         0x0C
#define SACC_REG_SQ_BASE_LO     0x10
#define SACC_REG_SQ_BASE_HI     0x14
#define SACC_REG_SQ_SIZE        0x18
#define SACC_REG_CQ_BASE_LO     0x20
#define SACC_REG_CQ_BASE_HI     0x24
#define SACC_REG_CQ_SIZE        0x28
#define SACC_REG_CQ_HEAD        0x30
#define SACC_REG_IRQ_STATUS     0x34
#define SACC_REG_IRQ_ACK        0x38
#define SACC_REG_ERR_CODE       0x3C
#define SACC_REG_SCRATCH        0x40

/* Doorbell page, BAR0 page 1 */
#define SACC_DB_PAGE_OFFSET     0x1000
#define SACC_DB_SQ_TAIL         (SACC_DB_PAGE_OFFSET + 0x00)

#define SACC_MAGIC_VAL          0x4A4E524F
#define SACC_VERS_VAL           0x00000001

/* CTRL bits */
#define SACC_CTRL_ENABLE        (1u << 0)
#define SACC_CTRL_RESET         (1u << 1)

/* STATUS bits */
#define SACC_STATUS_READY       (1u << 0)
#define SACC_STATUS_ERROR       (1u << 1)

/* IRQ bits */
#define SACC_IRQ_COMPLETION     (1u << 0)

/* Error codes */
#define SACC_ERR_OK             0x0
#define SACC_ERR_BAD_OPCODE     0x1
#define SACC_ERR_BAD_ADDR       0x2
#define SACC_ERR_BAD_LENGTH     0x3
#define SACC_ERR_NOT_ENABLED    0x4

#endif
