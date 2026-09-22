/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 Md Rayhanul Islam
 */

#ifndef RTE_MEM_SYNC_H
#define RTE_MEM_SYNC_H

/**
 * @file
 *
 * Cache maintenance for devices whose DMA is not coherent with the CPU
 * caches, in the shape the kernel DMA APIs use: the owner of a buffer hands
 * it over before the other side touches it.  Drivers call these only for
 * devices the bus reports as non-coherent; on every other platform they are
 * empty.
 */

#include <stddef.h>
#include <stdint.h>

#include <rte_common.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Size of a data cache line on this CPU, for callers that must group work by
 * line rather than write back memory the device may be writing.
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

#ifdef RTE_ARCH_ARM64

/*
 * Step by the smallest line any implementation may have: a larger real line
 * is still cleaned whole, only with redundant operations.
 */
static inline void
rte_mem_sync_for_device(const void *addr, size_t len)
{
	uintptr_t p = (uintptr_t)addr & ~(uintptr_t)(RTE_CACHE_LINE_MIN_SIZE - 1);
	uintptr_t end = (uintptr_t)addr + len;

	for (; p < end; p += RTE_CACHE_LINE_MIN_SIZE)
		asm volatile("dc cvac, %0" : : "r" (p) : "memory");
	asm volatile("dsb sy" : : : "memory");
}

/* EL0 has no invalidate-without-clean, so this also writes back dirty lines. */
static inline void
rte_mem_sync_for_cpu(const void *addr, size_t len)
{
	uintptr_t p = (uintptr_t)addr & ~(uintptr_t)(RTE_CACHE_LINE_MIN_SIZE - 1);
	uintptr_t end = (uintptr_t)addr + len;

	for (; p < end; p += RTE_CACHE_LINE_MIN_SIZE)
		asm volatile("dc civac, %0" : : "r" (p) : "memory");
	asm volatile("dsb sy" : : : "memory");
}

#else

static inline void
rte_mem_sync_for_device(const void *addr, size_t len)
{
	RTE_SET_USED(addr);
	RTE_SET_USED(len);
}

static inline void
rte_mem_sync_for_cpu(const void *addr, size_t len)
{
	RTE_SET_USED(addr);
	RTE_SET_USED(len);
}

#endif /* RTE_ARCH_ARM64 */

#ifdef __cplusplus
}
#endif

#endif /* RTE_MEM_SYNC_H */
