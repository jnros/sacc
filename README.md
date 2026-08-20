# SACC: Synthetic PCIe Accelerator

PCIe accelerator implemented two ways: as a device in QEMU and in a kernel driver. Both are based on spec.md, which is the authoratative source.

GPUs/Accelerators submit work through mapped command buffers and doorbell reads and writes. Control plane sits below the Linux kernel and inside proprietary drivers. 

SACC substitutes a device whose register map, ring semantics, and completion protocol are open, minimal, and built to spec.

Writeup: https://lineargp.com/notes/2026-08-synthetic-pcie/

Status: v0.1 minimal control path. Device identity, BAR layout, driver probe. Device boots in QEMU, enumerated in Linux, and device driver binds with PCI subsystem.

Build QEMU device: 
    Clone qemu source into $qemu
    $ cp qemu/sacc.c $qemu/hw/misc
    <add to hw/misc/meson.build and hw/misc/Kconfig>
    $ cd $qemu && ./configure, enable SACC
    $ make -j

Build kernel driver:

    (Kernel headers required)
    $ cd driver && make

Run: 
    ./run_qemu.sh - starts a q35 guest, pcie-root-port, and SACC behind it

    $ lspci -tv
    ...
    -+-[0000:00]-+-00.0  Intel Corporation 82G33/G31/P35/P31 Express DRAM Controller
    ...
    \-[0000:80]-+-00.0-[81]--
    ...
    +-06.0-[87]----00.0  Red Hat, Inc. Device 1092

    $ sudo insmod sacc.ko
    $ dmesg | tail -2
    sacc_pci_driver 0000:87:00.0: probe detected, initializing...
    sacc_pci_driver 0000:87:00.0: probe magic number validate 0x4A4E524F

Interface:

Bar0 is 8K. Page 0 4k holds register file. Page 1 4k holds doorbell. Completion ownership uses NVME-style phase bit.

Register map, descriptor formats, ownership rules, and memory barrier
requirements are specified in spec.md.

## License

GPL-2.0


    



