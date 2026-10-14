from scapy.all import Ether, IP, UDP, Raw, sendp
import time

IFACE = "tap1"
DST_MAC = "52:54:00:12:34:02"
SRC_MAC = "02:00:00:00:00:01"
BASE_SRC_IP = "10.0.0.1"
BASE_DST_IP = "10.0.0.2"
BASE_SRC_PORT = 5000
BASE_DST_PORT = 6000
DELAY = 0.03

def send_packet(src_ip=BASE_SRC_IP, dst_ip=BASE_DST_IP, sport=BASE_SRC_PORT, dport=BASE_DST_PORT, payload=b"RSS experiment"):
    packet = Ether(src=SRC_MAC, dst=DST_MAC) / IP(src=src_ip, dst=dst_ip) / UDP(sport=sport, dport=dport) / Raw(payload)
    sendp(packet, iface=IFACE, verbose=False)
    time.sleep(DELAY)

def send_group(name, packets):
    print("\n" + name)
    for index, packet in enumerate(packets, 1):
        print(f"{index}: {packet}")
        send_packet(**packet)

def flow(src_ip=BASE_SRC_IP, dst_ip=BASE_DST_IP, sport=BASE_SRC_PORT, dport=BASE_DST_PORT, payload=b"RSS experiment"):
    return {"src_ip": src_ip, "dst_ip": dst_ip, "sport": sport, "dport": dport, "payload": payload}

def main():
    send_group("EXPERIMENT 1A: SAME FLOW, 20 IDENTICAL PACKETS", [flow() for _ in range(20)])

    send_group("EXPERIMENT 1B: ALTERNATING BETWEEN TWO FLOWS", [
        flow(sport=5100 if i % 2 == 0 else 5101) for i in range(20)
    ])

    send_group("EXPERIMENT 2A: SOURCE PORT CHANGES", [
        flow(sport=5200 + i) for i in range(16)
    ])

    send_group("EXPERIMENT 2B: DESTINATION PORT CHANGES", [
        flow(dport=6100 + i) for i in range(16)
    ])

    send_group("EXPERIMENT 2C: SOURCE IP CHANGES", [
        flow(src_ip=f"10.0.{i // 250}.{(i % 250) + 1}") for i in range(1, 17)
    ])

    send_group("EXPERIMENT 2D: DESTINATION IP CHANGES", [
        flow(dst_ip=f"10.1.{i // 250}.{(i % 250) + 1}") for i in range(1, 17)
    ])

    send_group("EXPERIMENT 2E: PAYLOAD CHANGES ONLY", [
        flow(payload=f"payload variant {i}".encode()) for i in range(12)
    ])

    send_group("EXPERIMENT 2F: SOURCE AND DESTINATION PORTS CHANGE TOGETHER", [
        flow(sport=5300 + i, dport=6300 + i) for i in range(16)
    ])

    send_group("EXPERIMENT 3A: MANY FLOWS, SAME DESTINATION IP AND PORT", [
        flow(sport=5400 + i, dst_ip=BASE_DST_IP, dport=BASE_DST_PORT) for i in range(64)
    ])

    send_group("EXPERIMENT 3B: MANY SOURCE IPS, SAME DESTINATION IP AND PORT", [
        flow(src_ip=f"10.2.{i // 250}.{(i % 250) + 1}", sport=5500 + i, dport=BASE_DST_PORT)
        for i in range(1, 33)
    ])

    send_group("EXPERIMENT 3C: SAME DESTINATION, VARYING UDP SOURCE PORTS", [
        flow(sport=5600 + i, dport=BASE_DST_PORT) for i in range(32)
    ])

    print("\nFinished sending all experiments.")

if __name__ == "__main__":
    main()
