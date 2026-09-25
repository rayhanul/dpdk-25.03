/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2015 Intel Corporation.
 * Copyright 2013-2014 6WIND S.A.
 */

#ifndef _RTE_BUS_PCI_H_
#define _RTE_BUS_PCI_H_

/**
 * @file
 * PCI device & driver interface
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>

#include <rte_compat.h>
#include <rte_debug.h>
#include <rte_interrupts.h>
#include <rte_pci.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct rte_pci_device;
struct rte_pci_driver;
struct rte_pci_ioport;

struct rte_devargs;

/**
 * Map the PCI device resources in user space virtual memory address
 *
 * Note that driver should not call this function when flag
 * RTE_PCI_DRV_NEED_MAPPING is set, as EAL will do that for
 * you when it's on.
 *
 * @param dev
 *   A pointer to a rte_pci_device structure describing the device
 *   to use
 *
 * @return
 *   0 on success, negative on error and positive if no driver
 *   is found for the device.
 */
int rte_pci_map_device(struct rte_pci_device *dev);

/**
 * Unmap this device
 *
 * @param dev
 *   A pointer to a rte_pci_device structure describing the device
 *   to use
 */
void rte_pci_unmap_device(struct rte_pci_device *dev);

/**
 * Dump the content of the PCI bus.
 *
 * @param f
 *   A pointer to a file for output
 */
void rte_pci_dump(FILE *f);

/**
 * Check whether this device has a PCI capability list.
 *
 *  @param dev
 *    A pointer to rte_pci_device structure.
 *
 *  @return
 *    true/false
 */
__rte_experimental
bool rte_pci_has_capability_list(const struct rte_pci_device *dev);

/**
 * Find device's PCI capability.
 *
 *  @param dev
 *    A pointer to rte_pci_device structure.
 *
 *  @param cap
 *    Capability to be found, which can be any from
 *    RTE_PCI_CAP_ID_*, defined in librte_pci.
 *
 *  @return
 *  > 0: The offset of the next matching capability structure
 *       within the device's PCI configuration space.
 *  < 0: An error in PCI config space read.
 *  = 0: Device does not support it.
 */
__rte_experimental
off_t rte_pci_find_capability(const struct rte_pci_device *dev, uint8_t cap);

/**
 * Find device's PCI capability starting from a previous offset in PCI
 * configuration space.
 *
 *  @param dev
 *    A pointer to rte_pci_device structure.
 *
 *  @param cap
 *    Capability to be found, which can be any from
 *    RTE_PCI_CAP_ID_*, defined in librte_pci.
 *  @param offset
 *    An offset in the PCI configuration space from which the capability is
 *    looked for.
 *
 *  @return
 *  > 0: The offset of the next matching capability structure
 *       within the device's PCI configuration space.
 *  < 0: An error in PCI config space read.
 *  = 0: Device does not support it.
 */
__rte_experimental
off_t rte_pci_find_next_capability(const struct rte_pci_device *dev, uint8_t cap, off_t offset);

/**
 * Find device's extended PCI capability.
 *
 *  @param dev
 *    A pointer to rte_pci_device structure.
 *
 *  @param cap
 *    Extended capability to be found, which can be any from
 *    RTE_PCI_EXT_CAP_ID_*, defined in librte_pci.
 *
 *  @return
 *  > 0: The offset of the next matching extended capability structure
 *       within the device's PCI configuration space.
 *  < 0: An error in PCI config space read.
 *  = 0: Device does not support it.
 */
__rte_experimental
off_t rte_pci_find_ext_capability(const struct rte_pci_device *dev, uint32_t cap);

/**
 * Enables/Disables Bus Master for device's PCI command register.
 *
 *  @param dev
 *    A pointer to rte_pci_device structure.
 *  @param enable
 *    Enable or disable Bus Master.
 *
 *  @return
 *  0 on success, -1 on error in PCI config space read/write.
 */
__rte_experimental
int rte_pci_set_bus_master(const struct rte_pci_device *dev, bool enable);

/**
 * Enable/Disable PASID (Process Address Space ID).
 *
 * @param dev
 *   A pointer to a rte_pci_device structure.
 * @param offset
 *   Offset of the PASID external capability structure.
 * @param enable
 *   Flag to enable or disable PASID.
 *
 * @return
 *   0 on success, -1 on error in PCI config space read/write.
 */
__rte_internal
int rte_pci_pasid_set_state(const struct rte_pci_device *dev,
		off_t offset, bool enable);

/**
 * @internal
 * What the host bridge above a device says about its DMA: the window it
 * translates into, and whether the traffic is coherent with the CPU caches.
 * A zero size means the identity mapping every other platform uses.
 */
struct rte_pci_dma_info {
	uint64_t cpu_base;	/**< start of the window, CPU side. */
	uint64_t bus_base;	/**< the same address as the device sees it. */
	uint64_t size;		/**< window length, 0 if not translated. */
	bool noncoherent;	/**< the CPU caches need maintaining by hand. */
	bool unusable;		/**< properties this bus cannot honour. */
};

/**
 * @internal
 * Copy a device's DMA properties into driver-owned storage.  A driver that
 * supports secondary processes saves them in its shared data during the
 * primary's probe, since the bus context is not shared.
 *
 * @param dev
 *   The PCI device.
 * @param info
 *   Filled in with the properties of the bridge above it.
 */
__rte_internal
void rte_pci_get_dma_info(const struct rte_pci_device *dev,
		struct rte_pci_dma_info *info);

/**
 * @internal
 * Translate an IOVA range into an address the device can reach, or
 * RTE_BAD_IOVA if it falls outside the window.  Addresses outside a
 * translated window are unreachable, so a driver must check every one.
 *
 * @param info
 *   The device's DMA properties.
 * @param iova
 *   The address as EAL knows it.
 * @param len
 *   Length of the range, which must fit in the window as well.
 * @return
 *   The device-side address, or RTE_BAD_IOVA.
 */
static inline rte_iova_t
rte_pci_dma_iova(const struct rte_pci_dma_info *info, rte_iova_t iova,
		size_t len)
{
	uint64_t offset;

	if (iova == RTE_BAD_IOVA || len == 0)
		return RTE_BAD_IOVA;
	if (info->size == 0)
		return iova;
	if (iova < info->cpu_base)
		return RTE_BAD_IOVA;
	offset = iova - info->cpu_base;
	if (offset >= info->size || len > info->size - offset)
		return RTE_BAD_IOVA;
	return info->bus_base + offset;
}

/**
 * Read PCI config space.
 *
 * @param device
 *   A pointer to a rte_pci_device structure describing the device
 *   to use
 * @param buf
 *   A data buffer where the bytes should be read into
 * @param len
 *   The length of the data buffer.
 * @param offset
 *   The offset into PCI config space
 * @return
 *  Number of bytes read on success, negative on error.
 */
int rte_pci_read_config(const struct rte_pci_device *device,
		void *buf, size_t len, off_t offset);

/**
 * Write PCI config space.
 *
 * @param device
 *   A pointer to a rte_pci_device structure describing the device
 *   to use
 * @param buf
 *   A data buffer containing the bytes should be written
 * @param len
 *   The length of the data buffer.
 * @param offset
 *   The offset into PCI config space
 */
int rte_pci_write_config(const struct rte_pci_device *device,
		const void *buf, size_t len, off_t offset);

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice.
 *
 * Read from a MMIO PCI resource.
 *
 * @param device
 *   A pointer to a rte_pci_device structure describing the device
 *   to use.
 * @param bar
 *   Index of the IO PCI resource we want to access.
 * @param buf
 *   A data buffer where the bytes should be read into.
 * @param len
 *   The length of the data buffer.
 * @param offset
 *   The offset into MMIO space described by @bar.
 * @return
 *   Number of bytes read on success, negative on error.
 */
__rte_experimental
int rte_pci_mmio_read(const struct rte_pci_device *device, int bar,
		void *buf, size_t len, off_t offset);

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice.
 *
 * Write to a MMIO PCI resource.
 *
 * @param device
 *   A pointer to a rte_pci_device structure describing the device
 *   to use.
 * @param bar
 *   Index of the IO PCI resource we want to access.
 * @param buf
 *   A data buffer containing the bytes should be written.
 * @param len
 *   The length of the data buffer.
 * @param offset
 *   The offset into MMIO space described by @bar.
 * @return
 *   Number of bytes written on success, negative on error.
 */
__rte_experimental
int rte_pci_mmio_write(const struct rte_pci_device *device, int bar,
		const void *buf, size_t len, off_t offset);

/**
 * Initialize a rte_pci_ioport object for a pci device io resource.
 *
 * This object is then used to gain access to those io resources (see below).
 *
 * @param dev
 *   A pointer to a rte_pci_device structure describing the device
 *   to use.
 * @param bar
 *   Index of the io pci resource we want to access.
 * @param p
 *   The rte_pci_ioport object to be initialized.
 * @return
 *  0 on success, negative on error.
 */
int rte_pci_ioport_map(struct rte_pci_device *dev, int bar,
		struct rte_pci_ioport *p);

/**
 * Release any resources used in a rte_pci_ioport object.
 *
 * @param p
 *   The rte_pci_ioport object to be uninitialized.
 * @return
 *  0 on success, negative on error.
 */
int rte_pci_ioport_unmap(struct rte_pci_ioport *p);

/**
 * Read from a io pci resource.
 *
 * @param p
 *   The rte_pci_ioport object from which we want to read.
 * @param data
 *   A data buffer where the bytes should be read into
 * @param len
 *   The length of the data buffer.
 * @param offset
 *   The offset into the pci io resource.
 */
void rte_pci_ioport_read(struct rte_pci_ioport *p,
		void *data, size_t len, off_t offset);

/**
 * Write to a io pci resource.
 *
 * @param p
 *   The rte_pci_ioport object to which we want to write.
 * @param data
 *   A data buffer where the bytes should be read into
 * @param len
 *   The length of the data buffer.
 * @param offset
 *   The offset into the pci io resource.
 */
void rte_pci_ioport_write(struct rte_pci_ioport *p,
		const void *data, size_t len, off_t offset);

#ifdef __cplusplus
}
#endif

#endif /* _RTE_BUS_PCI_H_ */
