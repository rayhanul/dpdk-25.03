.. SPDX-License-Identifier: BSD-3-Clause
   Copyright(c) 2026 Md Rayhanul Islam

Running DPDK on Broadcom BCM2711 platforms
==========================================

The Broadcom BCM2711, used on the Raspberry Pi 4 and Compute Module 4,
needs two platform properties to be taken into account before a PCIe
device can DMA correctly.  Both are handled automatically; this page
describes what happens and how to check it.

PCIe inbound address translation
--------------------------------

The PCIe host bridge does not present system memory to devices at the CPU
physical addresses.  On a Compute Module 4 the host bridge node declares::

   $ hexdump -C /proc/device-tree/scb/pcie@7d500000/dma-ranges
   02000000 00000004 00000000  00000000 00000000  00000001 00000000

which places CPU physical address 0 at PCIe bus address ``0x4_0000_0000``.

In ``RTE_IOVA_PA`` mode, an IOVA must therefore be the CPU physical
address plus that offset.  EAL reads the translation from the host
bridge's ``dma-ranges`` property and applies it.  Platforms that declare
no translation, and platforms without a device tree, are unaffected.

A DPDK application logs the offset it found at startup::

   EAL: PCIe bus addresses are offset by 0x400000000 from CPU physical
   addresses (/proc/device-tree/scb/pcie@7d500000/dma-ranges); applying
   it to IOVAs

The value can be overridden with the ``DPDK_IOVA_PA_OFFSET`` environment
variable, given in hexadecimal.  Without the translation, a device
reports link up and counts packets in its own registers while never
completing a DMA.

Non-cache-coherent DMA
----------------------

The PCIe bus is not cache coherent: neither ``pcie@7d500000`` nor any of
its ancestors declares ``dma-coherent``.  A device therefore does not see
data the CPU has only written to its caches, and the CPU does not see
data the device has written to memory.

The ``e1000`` (igb) driver handles this when it detects a
device-tree-described device with no ``dma-coherent`` ancestor, on arm64.
It cleans the data cache over descriptors and packet data before the tail
register is written, and invalidates it before reading what the device
wrote.  Because a cache line covers four descriptors, TX completion is
taken from the hardware head register rather than the DD bits, and RX
descriptors are refilled one whole cache line at a time.

This is logged at device probe::

   E1000_INIT: 0000:01:00.0: PCIe DMA is not cache coherent,
   cleaning 64-byte D-cache lines before each DMA

It can be forced on or off with ``DPDK_DMA_NONCOHERENT=1`` or ``0``.

Other drivers running on this platform need equivalent handling; only
``e1000`` implements it today.

Recommended settings
--------------------

* Add ``pcie_aspm=off`` to the kernel command line, so the PCIe link does
  not enter a low-power state during a run.
* Bind the device to ``uio_pci_generic``.  There is no IOMMU on this
  SoC, so ``vfio-pci`` can only be used in unsafe no-IOMMU mode.
