# SACC: Synthetic PCIe Accelerator

PCIe accelerator implemented two ways: as a device in QEMU and in a kernel driver. Both are based on spec.md, which is the authoritative source.

GPUs/Accelerators submit work through mapped command buffers and doorbell writes. Control plane sits below the Linux kernel and inside proprietary drivers. 

SACC substitutes a device whose register map, ring semantics, and completion protocol are open, minimal, and built to spec.

Writeups:
- https://lineargp.com/notes/2026-08-synthetic-pcie/ (device, driver, topology)
- https://lineargp.com/notes/2026-10-pcie-doorbells-dma/ (rings, doorbells, DMA)

Status:
- v0.1 control path. Device identity, BAR layout, driver probe. Device boots
  in QEMU, enumerates in Linux, driver binds via PCI subsystem.
- Rings and doorbells. Single SQ/CQ pair (64 entries each) in host memory,
  allocated by driver. Driver writes NOOP descriptor, rings SQ tail doorbell.
  Device DMA-reads descriptors, DMA-writes completions. Driver polls CQ phase
  bit, writes CQ head. Probe runs 100 submit/poll iterations; rings wrap,
  phase flips.

Next: move submission/completion to userspace (full kernel bypass), MSI-X.

Layout:   
```
    spec.md         device specification 
    sacc_regs.h     shared register offsets and descriptors
    qemu/           PCIe device source code
    driver/         Linux driver source code
    run_qemu.sh     boot Linux guest with SACC device
```
Build QEMU device:   
```
    Clone qemu source into $qemu
    $ cp qemu/sacc.c $qemu/hw/misc
    <add to hw/misc/meson.build, hw/misc/Kconfig, hw/misc/trace-events>
    $ cd $qemu && ./configure, enable SACC
    $ make -j
```

Build kernel driver:   
```
    (Kernel headers required)
    $ cd driver && make
```
Run:   
```
    ./run_qemu.sh - starts a q35 guest, pcie-root-port, and SACC behind it

    $ lspci -tv
    ...
    -+-[0000:00]-+-00.0  Intel Corporation 82G33/G31/P35/P31 Express DRAM Controller
    ...
    \-[0000:80]-+-00.0-[81]--                                <-- second root complex (pxb-pcie)
    ...
    +-06.0-[87]----00.0  Red Hat, Inc. Device 1092           <-- SACC, device 1092

    $ sudo insmod sacc.ko
    $ dmesg | tail -2
    sacc_pci_driver 0000:87:00.0: probe detected, initializing...
    sacc_pci_driver 0000:87:00.0: probe magic number validate 0x4A4E524F
```
Trace (QEMU side, `-trace "sacc_*"`):
```
    sacc_doorbell sq_tail=1
    sacc_sq_fetch idx=0 ... req_id=0x1
    sacc_cq_post cq_tail=63 req_id=0x40 ... phase 0x1
    sacc_cq_post cq_tail=0 req_id=0x41 ... phase 0x0    <-- wrap, phase flips
```
Debug: QEMU monitor (`xp /8wx <addr>`) dumps guest memory, e.g. SQ
descriptors at SQ_BASE, to split driver bugs from device bugs.

Interface:   

BAR0 is 8K. Page 0 (4K) holds register file. Page 1 (4K) holds doorbell, so
userspace can mmap it without register access. Completion ownership uses
NVMe-style phase bit; host never reads device to check completion.

Register map, descriptor formats, ownership rules, and memory barrier
requirements are specified in spec.md.   

## License   

GPL-2.0


    



