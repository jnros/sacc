#!/bin/bash

if [ -n "$VIRTUAL_ENV" ]; then
    ENV_NAME=$(basename "$VIRTUAL_ENV")
    echo "Active environment detected: ($ENV_NAME)"
else
    { 
        echo "error: python virtual environment required"
        echo "Please run the following command to activate it:"
        echo "  source ~/envs/qemu/bin/activate"
    } >&2
        exit 1
fi

sudo /home/jnros/src/qemu/build/qemu-system-x86_64 \
-enable-kvm \
-cpu host \
-m 4G \
-smp 4 \
-machine q35,smm=on \
-drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
-drive if=pflash,format=raw,file=/var/lib/libvirt/qemu/nvram/labvm_VARS.fd \
-drive file=/var/lib/libvirt/images/labvm.qcow2,format=qcow2,if=none,id=disk0 \
-device pxb-pcie,id=pxb1,bus=pcie.0,bus_nr=64 \
-device pcie-root-port,id=rp1_1,bus=pxb1,chassis=2,slot=0 \
-device pcie-root-port,id=rp1_2,bus=pxb1,chassis=2,slot=1 \
-device pcie-root-port,id=rp1_3,bus=pxb1,chassis=2,slot=2 \
-device pcie-root-port,id=rp1_4,bus=pxb1,chassis=2,slot=3 \
-device pcie-root-port,id=rp1_5,bus=pxb1,chassis=2,slot=4 \
-device virtio-blk-pci,drive=disk0,bus=rp1_5 \
-device pcie-root-port,id=rp1_6,bus=pxb1,chassis=2,slot=5 \
-device pcie-root-port,id=rp1_7,bus=pxb1,chassis=2,slot=6 \
-device pcie-root-port,id=rp1_8,bus=pxb1,chassis=2,slot=7 \
-device pcie-root-port,id=rp1_9,bus=pxb1,chassis=2,slot=8 \
-device pcie-root-port,id=rp1_10,bus=pxb1,chassis=2,slot=9 \
-device pxb-pcie,id=pxb2,bus=pcie.0,bus_nr=128 \
-device pcie-root-port,id=rp2_1,bus=pxb2,chassis=3,slot=0 \
-device pcie-root-port,id=rp2_2,bus=pxb2,chassis=3,slot=1 \
-device pcie-root-port,id=rp2_3,bus=pxb2,chassis=3,slot=2 \
-device pcie-root-port,id=rp2_4,bus=pxb2,chassis=3,slot=3 \
-device pcie-root-port,id=rp2_5,bus=pxb2,chassis=3,slot=4 \
-device pcie-root-port,id=rp2_6,bus=pxb2,chassis=3,slot=5 \
-device pcie-root-port,id=rp2_7,bus=pxb2,chassis=3,slot=6 \
-device pcie-root-port,id=rp2_8,bus=pxb2,chassis=3,slot=7 \
-device pcie-root-port,id=rp2_9,bus=pxb2,chassis=3,slot=8 \
-device pcie-root-port,id=rp2_10,bus=pxb2,chassis=3,slot=9 \
-device pcie-root-port,id=rp2_11,bus=pxb2,chassis=3,slot=10 \
-device pcie-root-port,id=rp2_12,bus=pxb2,chassis=3,slot=11 \
-device pcie-root-port,id=rp2_13,bus=pxb2,chassis=3,slot=12 \
-device pcie-root-port,id=rp2_14,bus=pxb2,chassis=3,slot=13 \
-device sacc,bus=rp2_7 \
-nographic -netdev bridge,id=net0,br=br0 -device virtio-net-pci,netdev=net0,bus=rp1_2,mac=52:54:00:de:ad:01
