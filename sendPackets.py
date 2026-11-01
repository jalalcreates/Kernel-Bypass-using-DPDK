
from scapy.all import Ether, IP, UDP, Raw, sendp
import ipaddress
import struct

IFACE = "tap1"
DST_MAC = "52:54:00:12:34:02"
SRC_MAC = "02:00:00:00:00:01"
DST_IP = "10.0.0.2"
DST_PORT = 6000
PACKETS_PER_FLOW = 5000

RSS_KEY = bytes.fromhex(
    "6d 5a 56 da 25 5b 0e c2 41 67 25 3d 43 a3 8f b0 "
    "d0 ca 2b cb ae 7b 30 b4 77 cb 2d a3 80 30 f2 0c "
    "6a 42 b7 3b be ac 01 fa"
)

def toeplitz_hash(data):
    key = int.from_bytes(RSS_KEY, "big")
    value = int.from_bytes(data, "big")
    hash_value = 0
    data_bits = len(data) * 8

    for i in range(data_bits):
        if (value >> (data_bits - 1 - i)) & 1:
            hash_value ^= (key >> (len(RSS_KEY) * 8 - i - 32)) & 0xffffffff

    return hash_value

def find_flows():
    flows = {}

    for last_octet in range(1, 255):
        src_ip = f"10.2.0.{last_octet}"

        for src_port in range(5000, 6001):
            data = (
                ipaddress.IPv4Address(src_ip).packed
                + ipaddress.IPv4Address(DST_IP).packed
                + struct.pack("!HH", src_port, DST_PORT)
            )

            rss_hash = toeplitz_hash(data)
            reta_index = rss_hash & 0x7f
            queue = reta_index % 3

            if queue not in flows:
                flows[queue] = (
                    src_ip,
                    src_port,
                    rss_hash,
                    reta_index
                )

            if len(flows) == 3:
                return flows

    return flows

def main():
    flows = find_flows()

    if len(flows) != 3:
        print("Could not find flows for all three queues.")
        return

    for queue in range(3):
        src_ip, src_port, rss_hash, reta_index = flows[queue]

        print(
            f"Queue {queue}: {src_ip}:{src_port} "
            f"| Hash: 0x{rss_hash:08x} "
            f"| RETA: {reta_index}"
        )

    print("\nSending packets...\n")

    for queue in range(3):
        src_ip, src_port, _, _ = flows[queue]

        packet = (
            Ether(src=SRC_MAC, dst=DST_MAC)
            / IP(src=src_ip, dst=DST_IP)
            / UDP(sport=src_port, dport=DST_PORT)
            / Raw(f"RSS queue {queue} test".encode())
        )

        print(f"Sending {PACKETS_PER_FLOW} packets to Queue {queue}...")

        sendp(
            packet,
            iface=IFACE,
            count=PACKETS_PER_FLOW,
            inter=0,
            verbose=False
        )

        print(f"Queue {queue}: Transmission completed.")

    print("\nAll packets sent.")

if __name__ == "__main__":
    main()
