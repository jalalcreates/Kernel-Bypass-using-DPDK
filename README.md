# Kernel Bypass Packet Processing using DPDK

This project explores high-performance packet reception and multi-core packet processing using DPDK, running inside an Arch Linux virtual machine hosted through QEMU.

The idea is to receive packets directly through a DPDK poll-mode driver rather than processing them through the conventional Linux kernel networking stack. The application uses an emulated Intel 82576 network interface and distributes incoming traffic across three hardware RX queues using RSS. A main lcore polls the queues and dispatches received mbufs to three worker lcores through separate DPDK rings.

Each worker has an independent responsibility. One processes IPv4 packets, another parses UDP packets and accounts for their payload bytes, and the third classifies packets by size. This provides a simple but practical example of how packet reception, queue distribution, inter-core communication, and packet processing can be organized in a DPDK application.

## How the application works

The application configures three RX queues and three TX queues on the Ethernet device. RSS is enabled for non-fragmented IPv4 UDP traffic, allowing the NIC to calculate a hash from the packet's selected network and transport-layer fields and use it to select an RX queue. The initial idea was to use the TCAM through the rte_flow API but since this was not an available option in an emulated environment, I had to figure out a work around i.e. RSS.

The RSS configuration uses a 40-byte key and a 128-entry Receive Side Scaling Redirection Table (RETA). The RETA is configured with entries distributed across the three receive queues using `RETA[i] = i % 3`. This makes it possible to inspect how an RSS hash maps to a RETA index and ultimately to a hardware receive queue.

The main lcore continuously polls all three RX queues using `rte_eth_rx_burst()`. Received mbuf pointers are placed into the corresponding software ring using `rte_ring_enqueue_burst()`. Each ring has one producer and one consumer, so the rings are configured with `RING_F_SP_ENQ` and `RING_F_SC_DEQ`.

The three worker lcores independently dequeue packets from their assigned rings and perform their respective processing tasks.

## Implemented features

- DPDK EAL initialization and Ethernet device configuration.
- Three RX queues and three TX queues.
- RSS configuration for non-fragmented IPv4 UDP traffic.
- Inspection of NIC RSS capabilities through `rte_eth_dev_info` and `flow_type_rss_offloads`.
- RSS key inspection and RETA configuration.
- RSS hash extraction from received mbufs, with validity checked through `RTE_MBUF_F_RX_RSS_HASH`.
- RETA index calculation and verification of hash-to-queue mapping.
- Three dedicated worker lcores, each assigned a separate RX queue's software ring.
- Three DPDK rings using single-producer/single-consumer enqueue and dequeue modes.
- Burst-based packet reception and ring enqueue/dequeue operations.
- IPv4 packet identification and processing.
- UDP header parsing and payload-byte accounting.
- Packet-size classification into small and large packet counters.
- Independent statistics for each worker, including processed packets, IPv4 packets, UDP packets, UDP payload bytes, and packet-size classifications.
- Ring-full detection using the actual number of successfully enqueued mbufs.
- Explicit freeing of packets rejected because their destination ring is full.
- Correct mbuf ownership transfer between the producer and worker cores.
- Graceful shutdown using SIGINT/SIGTERM handling, worker ring draining, and lcore synchronization.
- Resource cleanup for rings, Ethernet device, mempool, and DPDK EAL.

## QEMU/KVM environment

The application was developed and tested inside an Arch Linux / Omarchy guest running under QEMU/KVM.

The VM has two network interfaces:

- A virtio network interface used for guest management and ordinary network connectivity.
- A dedicated emulated Intel 82576 Gigabit Ethernet Controller used by the DPDK application.

The dedicated NIC is exposed to the guest at PCI address `0000:00:04.0` and uses the DPDK `net_e1000_igb` poll-mode driver. The device is bound to `uio_pci_generic` rather than its normal kernel network driver, allowing DPDK to manage the device directly.

The environment uses DPDK 25.11.1-2, with 512 MiB of hugepages configured and `/dev/hugepages` available for DPDK memory allocation. The application uses physical addressing mode through the `--iova-mode=pa` EAL argument.

## Dependencies

On an Arch Linux guest, install the required build tools and DPDK packages:

```bash
sudo pacman -S --needed base-devel dpdk pkgconf
```

The project uses:

- DPDK EAL and lcore APIs
- DPDK Ethernet device (ethdev) APIs
- DPDK mbuf and mempool APIs
- DPDK ring APIs
- DPDK RSS and hash-related APIs
- C++17 standard library

The DPDK development headers and libraries are obtained through `pkg-config`.

## Building

From the project directory, compile the application using:

```bash
g++ -std=c++17 -Wall -Wextra -O2 \
    $(pkg-config --cflags libdpdk) \
    dpdk.cpp -o dpdk \
    $(pkg-config --libs libdpdk)
```

## Running

The application is configured to use four logical cores: one main polling lcore and three worker lcores.

Run it with:

```bash
sudo ./dpdk -l 0-3 -n 4 --iova-mode=pa -a 0000:00:04.0
```

Here:

- `-l 0-3` enables four logical cores.
- `-n 4` specifies the memory-channel count.
- `--iova-mode=pa` selects physical-address IOVA mode.
- `-a 0000:00:04.0` allows the specified PCI device to be used by DPDK.

The hugepages must be configured, the DPDK-compatible driver must be loaded, and the NIC must be bound to the appropriate driver before running the application.

## Testing

Packet traffic can be generated through the host-side TAP networking setup and directed towards the dedicated guest NIC. Multiple IPv4/UDP flows can be used to observe RSS hash generation and distribution across the three RX queues.

The application prints periodic statistics showing packets received by each queue, packets processed by each worker, protocol-specific counters, and packets rejected because of full rings.

The project is primarily focused on understanding DPDK's packet reception and processing model, RSS-based distribution, lcore execution, ring-based communication, and mbuf lifecycle management. The TX queues are configured as part of the Ethernet device setup, while the current processing pipeline focuses on RX and worker-side packet processing.
