# DPDK 25.03 on Raspberry Pi 4 (BCM2711)

This branch makes DPDK 25.03 work with a PCIe Ethernet NIC on the
Raspberry Pi 4 / Compute Module 4 (BCM2711), where upstream DPDK with
`uio_pci_generic` transmits nothing (`TX-packets: 0`) and loses received
data.

Everything below was found on one specific machine, described under
**Test system**. Claims are separated into what was measured there and
what was only read from the platform's own description.

## Branch

`bcm2711-dma-fix-v25.03`, based on DPDK v25.03.

## Test system

| | |
|---|---|
| Board | Raspberry Pi Compute Module 4 Rev 1.1, BCM2711, 4 GB RAM, CM4 IO Board |
| CPU | Cortex-A72 (`CPU part 0xd08`), 64-byte D-cache lines |
| NIC | Intel I210 (`8086:1533` rev 03) in the PCIe x1 slot, `0000:01:00.0` |
| OS | Ubuntu 22.04 arm64, kernel `5.15.0-1105-raspi`, `pcie_aspm=off` |
| DPDK mode | `uio_pci_generic`, IOVA=PA, no IOMMU, 2 MB hugepages |

No other board, NIC, RAM size or kernel was tested.

## Problem 1: the NIC does not see CPU physical addresses

**What the platform declares.** The PCIe host bridge's device tree node
`/proc/device-tree/scb/pcie@7d500000/dma-ranges` on this board contains:

```
02000000 00000004 00000000   00000000 00000000   00000001 00000000
└ PCI memory space ┘ └ bus 0x4_0000_0000 ┘ └ CPU 0x0 ┘ └ size 4 GiB ┘
```

So inbound (device-to-memory) traffic is translated: CPU physical address
`P` is reached by a device using bus address `P + 0x4_0000_0000`. For
reference, the outbound window in the same node's `ranges` starts at PCI
address `0xc000_0000`.

**What was measured.** In IOVA=PA mode DPDK hands the NIC CPU physical
addresses. The I210 then reported link up and incremented its own
registers, but never completed a DMA: `TX-packets` stayed 0 and received
data never appeared in the mbufs. Adding `0x4_0000_0000` to IOVAs made
transmission work immediately.

**The fix** (`lib/eal/linux/eal_memory.c`): read the translation from the
host bridge's `dma-ranges` in the device tree and add it to IOVAs in
`rte_mem_virt2iova()` and the legacy memory path. The value is not
hardcoded, so a platform that declares a different translation gets that
one, and a platform that declares none (or has no device tree, such as
x86) is unaffected. Override with `DPDK_IOVA_PA_OFFSET=<hex>`.

**Not verified:** whether the same offset appears on BCM2711 boards with
other RAM sizes, on other firmware versions, or on other SoCs with a
translating host bridge. The code reads whatever the board declares
rather than assuming this board's value, but only this board was tested.

## Problem 2: the PCIe bus is not cache coherent

**What the platform declares.** Neither `pcie@7d500000` nor any ancestor
in this board's device tree has a `dma-coherent` property, which by the
device tree binding means the device's DMA is not coherent with the CPU
caches. Linux handles this with cache maintenance in the DMA API; DPDK
writes descriptors and packet data into hugepage memory and rings the
doorbell without it.

**What was measured on this board**, once the address offset was in place:

- The NIC fetched stale descriptor contents — zeroed descriptors with the
  DD bit set and null buffer addresses — so transmits never completed and
  received data was DMA'd to address 0.
- `DC CVAU` (clean to the point of unification) was **not** sufficient.
  Only `DC CVAC` (clean to the point of coherency, what arm64's
  `__dma_clean_area` uses) made the NIC read what the CPU had written.
- With 4 descriptors per 64-byte cache line, cleaning a line that the CPU
  had just written also wrote back stale copies of its neighbours and
  erased DD bits the NIC had just set. So DD could not be used for TX
  completion, and RX refills could not be written back descriptor by
  descriptor.

**The fix** (`drivers/net/intel/e1000/igb_rxtx.c`), applied only when the
device is device-tree-described and no ancestor declares `dma-coherent`,
on arm64 builds:

- `DC CVAC` over descriptors and packet data before the TDT/RDT write
  that lets the NIC read them.
- `DC CIVAC` over descriptor status and received data before the CPU
  reads what the NIC wrote.
- TX completion taken from the hardware head register (TDH) instead of
  the DD bits.
- RX buffer addresses written back one whole cache line at a time, and
  only once every descriptor in that line has been returned by the NIC
  and consumed.

This is a no-op on coherent platforms and on non-arm64 builds. Override
with `DPDK_DMA_NONCOHERENT=0|1`.

**Not verified:** other Intel PMDs. The same reasoning should apply to
any driver that writes descriptors from the CPU on a non-coherent bus,
but only `igb` was changed and tested. `igc` (I225/I226) is untouched.

## Results on the test system

- `dpdk-testpmd --forward-mode=txonly`: about 1.42 Mpps with 64-byte
  frames, which is 1 GbE line rate, 0 TX errors.
- 300k-frame MAC loopback through the I210 with no loss.
- ICMPv6 echo round trip with a link partner, payloads byte-exact.
- Before the change, on the same machine: `TX-packets: 0`.

A DPDK application built on this branch logs both conditions at startup,
which is the quickest way to tell the fix is active:

```
EAL: PCIe bus addresses are offset by 0x400000000
E1000_INIT: 0000:01:00.0: PCIe DMA is not cache coherent, cleaning 64-byte D-cache lines before each DMA
```

## Author

Md Rayhanul Islam
University of Connecticut
