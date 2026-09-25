/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 Md Rayhanul Islam
 */

#ifndef RTE_MEM_SYNC_H
#define RTE_MEM_SYNC_H

/**
 * @file
 *
 * Cache maintenance for devices whose DMA is not coherent with the CPU
 * caches: the owner of a buffer hands it over before the other side touches
 * it, as dma_sync_single_for_{device,cpu}() and bus_dmamap_sync() do.
 *
 * Ownership is per cache line, so a buffer handed to a device must not share
 * a line with anything the CPU writes meanwhile.  Handing a buffer over does
 * not wait for the transfer: the caller still has to establish completion,
 * from a descriptor status or an interrupt, before reading what arrived.
 *
 * Empty on coherent platforms.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <rte_common.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Transfer direction, as the device sees it. */
enum rte_mem_sync_direction {
	RTE_MEM_SYNC_TO_DEVICE,		/**< the device reads the buffer. */
	RTE_MEM_SYNC_FROM_DEVICE,	/**< the device writes the buffer. */
	RTE_MEM_SYNC_BIDIRECTIONAL,	/**< both, or not known. */
};

/**
 * Size of a data cache line on this CPU, for callers that group work by line.
 */
static inline size_t
rte_mem_dcache_line_size(void)
{
#ifdef RTE_ARCH_ARM64
	uint64_t ctr;

	/* CTR_EL0.DminLine is log2 of the line size in words. */
	asm volatile("mrs %0, ctr_el0" : "=r" (ctr));
	return 4U << ((ctr >> 16) & 0xf);
#else
	return RTE_CACHE_LINE_SIZE;
#endif
}

/** Whether this build implements the maintenance below. */
static inline bool
rte_mem_sync_supported(void)
{
#if defined(RTE_ARCH_ARM64) && defined(RTE_EXEC_ENV_LINUX)
	return true;
#else
	return false;
#endif
}

#if defined(RTE_ARCH_ARM64) && defined(RTE_EXEC_ENV_LINUX)

/**
 * Hand [addr, addr + len) to the device.  A buffer the device writes is also
 * cleaned, not just invalidated: EL0 cannot execute DC IVAC, so a line the
 * CPU left dirty would otherwise land on top of what the device delivers.
 */
static inline void
rte_mem_sync_for_device(const void *addr, size_t len,
		enum rte_mem_sync_direction direction)
{
	/*
	 * Step by the smallest line any implementation may have, rather than
	 * asking the CPU for its own: reading CTR_EL0 here would cost more
	 * than the redundant operations on a machine with wider lines.
	 */
	uintptr_t p = (uintptr_t)addr & ~(uintptr_t)(RTE_CACHE_LINE_MIN_SIZE - 1);
	uintptr_t last = ((uintptr_t)addr + len - 1) &
			~(uintptr_t)(RTE_CACHE_LINE_MIN_SIZE - 1);

	if (len == 0)
		return;
	for (;;) {
		if (direction == RTE_MEM_SYNC_TO_DEVICE)
			asm volatile("dc cvac, %0" : : "r" (p) : "memory");
		else
			asm volatile("dc civac, %0" : : "r" (p) : "memory");
		if (p == last)
			break;
		p += RTE_CACHE_LINE_MIN_SIZE;
	}
	/* DMB, which the DPDK barriers issue, does not order this. */
	asm volatile("dsb sy" : : : "memory");
}

/** Take [addr, addr + len) back from the device, once the transfer is done. */
static inline void
rte_mem_sync_for_cpu(const void *addr, size_t len,
		enum rte_mem_sync_direction direction)
{
	if (direction != RTE_MEM_SYNC_TO_DEVICE)
		rte_mem_sync_for_device(addr, len, RTE_MEM_SYNC_FROM_DEVICE);
}

#else

static inline void
rte_mem_sync_for_device(const void *addr, size_t len,
		enum rte_mem_sync_direction direction)
{
	RTE_SET_USED(addr);
	RTE_SET_USED(len);
	RTE_SET_USED(direction);
}

static inline void
rte_mem_sync_for_cpu(const void *addr, size_t len,
		enum rte_mem_sync_direction direction)
{
	RTE_SET_USED(addr);
	RTE_SET_USED(len);
	RTE_SET_USED(direction);
}

#endif /* RTE_ARCH_ARM64 && RTE_EXEC_ENV_LINUX */

#ifdef __cplusplus
}
#endif

#endif /* RTE_MEM_SYNC_H */
