/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 Md Rayhanul Islam
 */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <rte_byteorder.h>
#include <rte_common.h>
#include <rte_string_fns.h>

#include "pci_init.h"
#include "private.h"

/* Read a device tree property, returning its length or a negative errno. */
static int
dt_read(const char *node, const char *name, void *buf, size_t size)
{
	char path[PATH_MAX];
	size_t len;
	int ret;
	FILE *f;

	if (snprintf(path, sizeof(path), "%s/%s", node, name) >= (int)sizeof(path))
		return -ENAMETOOLONG;
	f = fopen(path, "rb");
	if (f == NULL)
		return -errno;
	len = fread(buf, 1, size, f);
	ret = ferror(f) ? -EIO : (int)len;
	if (ret >= 0 && fgetc(f) != EOF)
		ret = -E2BIG;		/* longer than this parser handles */
	fclose(f);
	return ret;
}

static bool
dt_is_known_bridge(const char *node)
{
	static const char * const compatible[] = { "brcm,bcm2711-pcie" };
	char buf[256];
	size_t pos, n, i;
	int len;

	len = dt_read(node, "compatible", buf, sizeof(buf));
	if (len <= 0 || buf[len - 1] != '\0')
		return false;
	for (pos = 0; pos < (size_t)len; pos += n + 1) {	/* NUL-separated */
		n = strlen(buf + pos);
		for (i = 0; i < RTE_DIM(compatible); i++)
			if (strcmp(buf + pos, compatible[i]) == 0)
				return true;
	}
	return false;
}

static int
dt_cells(const char *node, const char *name)
{
	rte_be32_t value;

	if (dt_read(node, name, &value, sizeof(value)) != sizeof(value))
		return -EINVAL;
	return rte_be_to_cpu_32(value);
}

static uint64_t
dt_address(const rte_be32_t *cells, int n)
{
	uint64_t value = 0;

	while (n-- > 0)
		value = (value << 32) | rte_be_to_cpu_32(*cells++);
	return value;
}

/*
 * Read the one memory window a bridge declares in "dma-ranges".  An entry is
 * <pci-address> <parent-address> <size>: 3 cells, then the parent's
 * #address-cells, then this node's #size-cells.
 */
static int
dt_dma_window(const char *node, struct rte_pci_dma_info *info)
{
	char parent[PATH_MAX], *slash;
	rte_be32_t cells[7];
	int ac, len;

	strlcpy(parent, node, sizeof(parent));
	slash = strrchr(parent, '/');
	if (slash == NULL || slash == parent)
		return -EINVAL;
	*slash = '\0';

	ac = dt_cells(parent, "#address-cells");
	if ((ac != 1 && ac != 2) || dt_cells(node, "#address-cells") != 3 ||
			dt_cells(node, "#size-cells") != 2)
		return -ENOTSUP;

	len = dt_read(node, "dma-ranges", cells, sizeof(cells));
	if (len < 0)
		return len;
	/* One directly addressed memory window is all this handles. */
	if (len != (3 + ac + 2) * (int)sizeof(cells[0]) ||
			(rte_be_to_cpu_32(cells[0]) & 0x03000000) != 0x02000000)
		return -ENOTSUP;

	info->bus_base = dt_address(cells + 1, 2);
	info->cpu_base = dt_address(cells + 3, ac);
	info->size = dt_address(cells + 3 + ac, 2);
	if (info->size == 0 || info->size > UINT64_MAX - info->cpu_base ||
			info->size > UINT64_MAX - info->bus_base)
		return -EINVAL;
	return 0;
}

/* Coherency is inherited: the nearest ancestor that declares it wins. */
static bool
dt_is_coherent(const char *node)
{
	char path[PATH_MAX], value;
	char *slash;

	strlcpy(path, node, sizeof(path));
	for (;;) {
		if (dt_read(path, "dma-coherent", &value, sizeof(value)) >= 0)
			return true;
		if (dt_read(path, "dma-noncoherent", &value, sizeof(value)) >= 0)
			return false;
		slash = strrchr(path, '/');
		if (slash == NULL || slash == path)
			return false;
		*slash = '\0';
	}
}

/*
 * Record what the bridge above this device says about its DMA.  A device that
 * sits behind no known bridge keeps the identity mapping and is left coherent,
 * which is what every platform did before.  Properties this parser cannot make
 * sense of mark the device unusable rather than failing the scan: the bus
 * refuses that one device at probe and everything else carries on.
 */
void
pci_dt_read_dma_info(struct rte_pci_device *dev, const char *dirname)
{
	struct rte_pci_dma_info *info = &RTE_PCI_DEVICE_INTERNAL(dev)->dma;
	char node[PATH_MAX], path[PATH_MAX], of_node[PATH_MAX];
	char *slash;
	int ret;

	memset(info, 0, sizeof(*info));
	if (realpath(dirname, node) == NULL)
		return;

	while ((slash = strrchr(node, '/')) != NULL && slash != node) {
		if (snprintf(path, sizeof(path), "%s/of_node", node) <
				(int)sizeof(path) &&
				realpath(path, of_node) != NULL &&
				dt_is_known_bridge(of_node)) {
			ret = dt_dma_window(of_node, info);
			if (ret < 0) {
				PCI_LOG(ERR, "%s: cannot use the DMA properties of %s",
					dev->name, of_node);
				info->unusable = true;
				return;
			}
			info->noncoherent = !dt_is_coherent(of_node);
			return;
		}
		*slash = '\0';
	}
}
