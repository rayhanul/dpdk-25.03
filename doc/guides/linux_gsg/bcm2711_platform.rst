..  SPDX-License-Identifier: BSD-3-Clause
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
address plus that offset.  The PCI bus reads ``dma-ranges`` from the host
bridge above each device and reports the translation to EAL, which adds
it to every IOVA.  Platforms that declare no translation, and platforms
without a device tree, are unaffected.

The offset is logged once at startup::

   EAL: Device addresses are offset by 0x400000000 from physical addresses
   (/sys/firmware/devicetree/base/scb/pcie@7d500000)

Only one translation can be in force, because IOVAs are a single address
space: if a second bridge declares a different offset, EAL reports the
conflict and initialization fails.  Without the translation, a device
reports link up and counts packets in its own registers while never
completing a DMA.

Non-cache-coherent DMA
----------------------

The PCIe bus is not cache coherent: neither ``pcie@7d500000`` nor any of
its ancestors declares ``dma-coherent``.  A device therefore does not see
data the CPU has only written to its caches, and the CPU does not see
data the device has written to memory.

The PCI bus reports this per device through ``rte_pci_dma_is_coherent()``,
and EAL provides the cache maintenance in ``rte_mem_sync.h``.  A driver
that does nothing about it will move corrupt data.

The ``e1000`` driver handles it in ``igb``, which installs separate burst
functions at probe so that coherent platforms keep the paths they had.  They write descriptors and packet data back to memory before the
tail register is written, and invalidate them before reading what the
device wrote.  Because one 64-byte cache line covers four descriptors,
TX completion is taken from the DD bits with the last requested
descriptor as the in-order fallback, and an RX buffer is written back
before it is handed to the hardware again.

This is logged at device probe::

   E1000_INIT: 0000:01:00.0: DMA is not cache coherent,
   maintaining 64-byte D-cache lines

``em`` does not implement the maintenance and refuses to probe such a
device rather than corrupt traffic.  Other drivers running on this
platform need equivalent handling.

Recommended settings
--------------------

* Add ``pcie_aspm=off`` to the kernel command line, so the PCIe link does
  not enter a low-power state during a run.
* Bind the device to ``uio_pci_generic``.  There is no IOMMU on this
  SoC, so ``vfio-pci`` can only be used in unsafe no-IOMMU mode.
