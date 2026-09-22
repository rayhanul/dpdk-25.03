/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 Md Rayhanul Islam
 */

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <rte_common.h>
#include <rte_byteorder.h>
#include <rte_memory.h>
#include <rte_pci.h>
#include <rte_bus_pci.h>
#include <eal_export.h>

#include <rte_string_fns.h>

#include "pci_init.h"
#include "private.h"

static int
dt_read_prop(const char *dir, const char *prop, void *buf, size_t buflen,
		size_t *outlen)
{
	char path[PATH_MAX];
	size_t n;
	FILE *f;

	if (snprintf(path, sizeof(path), "%s/%s", dir, prop) >= (int)sizeof(path))
		return -1;
	f = fopen(path, "rb");
	if (f == NULL)
		return -1;
	n = fread(buf, 1, buflen, f);
	if (ferror(f)) {
		fclose(f);
		return -1;
	}
	fclose(f);
	*outlen = n;
	return 0;
}

static int
dt_read_u32(const char *dir, const char *prop, uint32_t *out)
{
	uint32_t val;
	size_t len;

	if (dt_read_prop(dir, prop, &val, sizeof(val), &len) < 0 ||
			len != sizeof(val))
		return -1;
	*out = rte_be_to_cpu_32(val);
	return 0;
}

static uint64_t
dt_read_cells(const uint32_t *cells, uint32_t n)
{
	uint64_t val = 0;
	uint32_t i;

	for (i = 0; i < n; i++)
		val = (val << 32) | rte_be_to_cpu_32(cells[i]);
	return val;
}

/* -ENOENT: no "dma-ranges" here.  -EINVAL: one that is not a single offset. */
static int
dt_bridge_dma_offset(const char *node, uint64_t *offset)
{
	char parent[PATH_MAX], *slash;
	uint32_t cells[256];
	uint32_t parent_ac, size_c;
	size_t len, ncells, per_entry, i;
	uint64_t off = 0;
	bool first = true;

	if (dt_read_prop(node, "dma-ranges", cells, sizeof(cells), &len) < 0)
		return -ENOENT;

	if (len == 0) {
		*offset = 0;
		return 0;
	}

	strlcpy(parent, node, sizeof(parent));
	slash = strrchr(parent, '/');
	if (slash == NULL || slash == parent)
		return -EINVAL;
	*slash = '\0';

	if (dt_read_u32(parent, "#address-cells", &parent_ac) < 0 ||
			dt_read_u32(node, "#size-cells", &size_c) < 0)
		return -EINVAL;
	if (parent_ac == 0 || parent_ac > 2 || size_c > 2)
		return -EINVAL;

	per_entry = 3 + parent_ac + size_c;
	ncells = len / sizeof(uint32_t);
	if (ncells == 0 || ncells % per_entry != 0)
		return -EINVAL;

	for (i = 0; i < ncells; i += per_entry) {
		uint64_t bus_addr = dt_read_cells(&cells[i + 1], 2);
		uint64_t cpu_addr = dt_read_cells(&cells[i + 3], parent_ac);
		uint64_t entry_off = bus_addr - cpu_addr;

		if (first) {
			off = entry_off;
			first = false;
		} else if (entry_off != off) {
			PCI_LOG(ERR, "%s: non-uniform dma-ranges", node);
			return -EINVAL;
		}
	}

	*offset = off;
	return 0;
}

static const char * const dt_bridge_compatible[] = {
	"brcm,bcm2711-pcie",
};

static bool
dt_bridge_is_known(const char *node)
{
	char compat[256];
	size_t len, i, pos;

	if (dt_read_prop(node, "compatible", compat, sizeof(compat) - 1, &len) < 0)
		return false;
	compat[len] = '\0';

	/* NUL-separated list. */
	for (pos = 0; pos < len; pos += strlen(&compat[pos]) + 1)
		for (i = 0; i < RTE_DIM(dt_bridge_compatible); i++)
			if (strcmp(&compat[pos], dt_bridge_compatible[i]) == 0)
				return true;
	return false;
}

RTE_EXPORT_INTERNAL_SYMBOL(rte_pci_dma_is_coherent)
bool
rte_pci_dma_is_coherent(const struct rte_pci_device *dev)
{
	char path[PATH_MAX], node[PATH_MAX], prop[PATH_MAX];
	bool described = false;
	char *slash;

	if (snprintf(path, sizeof(path), "%s/" PCI_PRI_FMT,
			rte_pci_get_sysfs_path(), dev->addr.domain, dev->addr.bus,
			dev->addr.devid, dev->addr.function) >= (int)sizeof(path) ||
			realpath(path, node) == NULL)
		return true;

	while ((slash = strrchr(node, '/')) != NULL && slash != node) {
		if (snprintf(prop, sizeof(prop), "%s/of_node", node) <
				(int)sizeof(prop) && access(prop, F_OK) == 0) {
			described = true;
			if (snprintf(prop, sizeof(prop), "%s/of_node/dma-coherent",
					node) < (int)sizeof(prop) &&
					access(prop, F_OK) == 0)
				return true;
		}
		*slash = '\0';
	}
	return !described;
}

/* Report the translation of the bridge this device sits behind. */
int
pci_dt_set_dma_offset(const char *dirname)
{
	char path[PATH_MAX], node[PATH_MAX], of_node[PATH_MAX];
	uint64_t offset;
	char *slash;

	if (realpath(dirname, node) == NULL)
		return 0;

	while ((slash = strrchr(node, '/')) != NULL && slash != node) {
		if (snprintf(of_node, sizeof(of_node), "%s/of_node", node) <
				(int)sizeof(of_node) &&
				realpath(of_node, path) != NULL &&
				dt_bridge_is_known(path)) {
			int ret = dt_bridge_dma_offset(path, &offset);

			if (ret == 0)
				return rte_mem_set_iova_pa_offset(offset, path);
			if (ret != -ENOENT)
				return ret;
		}
		*slash = '\0';
	}
	return 0;
}
