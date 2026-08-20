# SACC - Synthetic PCIe Accelerator Control Plane (v0.1)

**Status:** LOCKED. This document is the source of truth for both device and driver.

---

## 1. Device Identity

| Field         | Value         | Notes                                                                    |
| ------------- | ------------- | ------------------------------------------------------------------------ |
| Vendor ID     | 0x1B36        | Red Hat/QEMU                                                             |
| Device ID     | 0x1092        |                                                                          |
| Class code    | 0x120000      | Processing Accelerator                                                   |
| Revision      | 0x01          | Bump on breaking interface changes                                       |
| Subsystem IDs | TBD           | Default to Vendor/Device ID                                              |
| Device Type   | PCIe Endpoint | PCIe v2 capability, 4 KB config space. Sits behind pcie-root-port on Q35 |
## **1.1 Config Space Layout**

| Offset    | Contents                              |
| --------- | ------------------------------------- |
| 0x00–0x3F | Standard PCI header                   |
| 0x40–0x7B | PCIe capability (v2, endpoint)        |
| 0x7C–0xFF | Reserved                              |
| 0x100+    | Extended config space (empty in v0.1) |

MSI-X capability offset is dynamically allocated. MSI-X table and PBA reside in BAR1.

## 2. BAR Layout

| BAR  | Size | Type     | Contents                                               |
| ---- | ---- | -------- | ------------------------------------------------------ |
| BAR0 | 8 KB | MMIO     | Page 0 (0x0000): registers. Page 1 (0x1000): doorbell. |
| BAR1 | 4 KB | MMIO     | MSI-X table (offset 0) & PBA                           |
| BAR2 | —    | Reserved | reserved for future direct-mapped window               |

Bulk transfers use DMA via descriptor rings. BARs carry registers only.

## 3. Register File (BAR0)

Little-endian. Reserved offsets read 0 and ignore writes.

| Offset | Name       | Access | Width | Description                                     |
| ------ | ---------- | ------ | ----- | ----------------------------------------------- |
| 0x00   | MAGIC      | RO     | 32    | 0x4A4E524F                                      |
| 0x04   | VERSION    | RO     | 32    | Interface version (0x00000001)                  |
| 0x08   | CTRL       | RW     | 32    | Bit 0: ENABLE. Bit 1: RESET (self-clearing)     |
| 0x0C   | STATUS     | RO     | 32    | Bit 0: READY. Bit 1: ERROR                      |
| 0x10   | SQ_BASE    | RW     | 64    | Submission ring base (DMA address)              |
| 0x18   | SQ_SIZE    | RW     | 32    | SQ descriptor count (power of 2)                |
| 0x20   | CQ_BASE    | RW     | 64    | Completion ring base (DMA address)              |
| 0x28   | CQ_SIZE    | RW     | 32    | CQ entry count (power of 2)                     |
| 0x30   | CQ_HEAD    | WO     | 32    | Consumed completion index (flow control)        |
| 0x34   | IRQ_STATUS | RO     | 32    | Bit 0: completion pending (read does not clear) |
| 0x38   | IRQ_ACK    | WO     | 32    | Write 1 to bit 0 to clear IRQ condition         |
| 0x3C   | ERR_CODE   | RO     | 32    | Last error code (Section 8)                     |

### 3.1 Doorbell Page (BAR0 page 1, offset 0x1000)

| Offset        | Name               | Access | Width | Description             |
| ------------- | ------------------ | ------ | ----- | ----------------------- |
| 0x1000        | SQ_TAIL (DOORBELL) | WO     | 32    | Tail index update       |
| 0x1004–0x1FFF | reserved           | —      | —     | Reads 0, writes ignored |

Isolated on a dedicated 4 KB page to allow userspace mmap without exposing control registers.

## 4. Submission Descriptor Format

Contiguous ring of SQ_SIZE descriptors at SQ_BASE.

| Bytes | Field    | Description                              |
| ----- | -------- | ---------------------------------------- |
| 0–7   | src_addr | Guest-physical address of input buffer   |
| 8–15  | dst_addr | Guest-physical address of output buffer  |
| 16–19 | length   | Bytes to process                         |
| 20–21 | opcode   | Section 6                                |
| 22–23 | flags    | Bit 0: interrupt on completion           |
| 24–27 | req_id   | Driver-chosen tag (echoed in completion) |
| 28–31 | reserved | Write as 0                               |

## 5. Completion Entry Format

Fixed 16 bytes. Ring is CQ_SIZE contiguous entries at CQ_BASE.

| Bytes | Field      | Description              |
| ----- | ---------- | ------------------------ |
| 0–3   | req_id     | Echoed submission req_id |
| 4–7   | result_len | Bytes written            |
| 8–11  | reserved   | Written as 0             |
| 12–13 | status     | Execution status         |
| 14–15 | phase      | Bit 0: phase flag        |

## 6. Opcodes (v0.1)

| Opcode        | Name     | Semantics                                  |
| ------------- | -------- | ------------------------------------------ |
| 0x0000        | NOP      | No-op.  Returns OK, result_len = 0.        |
| 0x0001        | MEMCPY   | DMA src to dst                             |
| 0x0002        | XFORM    | DMA src to dst, XORing each byte with 0x17 |
| 0x0100–0x01FF | reserved | Future inf                                 |
| 0x0200–0x02FF | reserved | Future kv                                  |

## 7. Rules of Engagement (ownership & ordering)

1. SQ Ownership: Driver owns slots until writing SQ_TAIL; device owns until posting completion
2. Device processes descriptors strictly in ring order
3. CQ ownership and phase protocol (NVMe-style)  
	- Ring starts zeroed; expected phase starts at `1`.
    - Device writes completion fields, setting `phase` bit LAST
    - Driver detects new completions when entry `phase` matches expected phase
    - Device and driver invert their respective phase bit on ring wrap
    - Device stalls when CQ is full (must not overwrite unconsumed entries).
4. Memory barriers: Driver must issue a write memory barrier ( wmb() ) before writing SQ_TAIL .
5. Reset: CTRL.RESET aborts active work, clears rings state, returns device to READY. Driver must reprogram rings after.

## 8. Error Codes

| Code | Meaning                 |
| ---- | ----------------------- |
| 0x0  | OK                      |
| 0x1  | Bad opcode              |
| 0x2  | Bad address / DMA fault |
| 0x3  | Invalid length          |
| 0x4  | Device not enabled      |

## 9. Interrupts

- MSI-X: Vector 0 triggers completion alerts when descriptor interrupt flag is set. Acked via IRQ_ACK.
- INTx: Not supported
- Polling mode: Supported by leaving the interrupt flag cleared in submission descriptors.

## 10. Initialization Sequence (driver's obligations)

1. Verify MAGIC and VERSION.
2. Allocate and zero SQ and CQ via dma_alloc_coherent.
3. Program SQ_BASE, SQ_SIZE, CQ_BASE, CQ_SIZE.
4. Enable MSI-X and register handler.
5. Set CTRL.ENABLE; wait for STATUS.READY.

## 11. Explicit Non-Goals (v0.1)

- Scatter-gather (contiguous buffers only)
- Multiple queues, QoS, SR-IOV
- Performance optimizations (every doorbell causes a VM exit)

## Changelog

- v0.1 — decisions locked: Vendor 0x1B36; 8 KB BAR0 with isolated doorbell page. NVMe phase-bit CQ sync.
- v0.1.1 — Defined PCIe endpoint topology behind pcie-root-port on q35.
