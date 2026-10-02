/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 Md Rayhanul Islam
 */

#ifndef RTE_MEM_SYNC_H
#define RTE_MEM_SYNC_H

/**
 * @file
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice.
 *
 * Cache maintenance for devices whose DMA is not coherent with the CPU
 * caches, after dma_sync_single_for_{device,cpu}().  Ownership is per cache
 * line, and a sync does not wait for the transfer to complete.  Empty on
 * coherent platforms.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <rte_common.h>
#include <rte_compat.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Transfer direction, as the device sees it. */
enum rte_mem_sync_direction {
	RTE_MEM_SYNC_TO_DEVICE,
	RTE_MEM_SYNC_FROM_DEVICE,
	RTE_MEM_SYNC_BIDIRECTIONAL,
};

/** Size of a data cache line on this CPU. */
__rte_experimental
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
__rte_experimental
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
 * Hand [addr, addr + len) to the device.  EL0 cannot execute DC IVAC, so a
 * buffer the device writes is cleaned as well as invalidated.
 */
__rte_experimental
static inline void
rte_mem_sync_for_device(const void *addr, size_t len,
		enum rte_mem_sync_direction direction)
{
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
	/* DMB does not order cache maintenance. */
	asm volatile("dsb sy" : : : "memory");
}

/** Take [addr, addr + len) back from the device once the transfer is done. */
__rte_experimental
static inline void
rte_mem_sync_for_cpu(const void *addr, size_t len,
		enum rte_mem_sync_direction direction)
{
	if (direction != RTE_MEM_SYNC_TO_DEVICE)
		rte_mem_sync_for_device(addr, len, RTE_MEM_SYNC_FROM_DEVICE);
}

#else

__rte_experimental
static inline void
rte_mem_sync_for_device(const void *addr, size_t len,
		enum rte_mem_sync_direction direction)
{
	RTE_SET_USED(addr);
	RTE_SET_USED(len);
	RTE_SET_USED(direction);
}

__rte_experimental
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
