from scapy.all import Ether, IP, UDP, Raw, sendp

IFACE = "tap1"

DST_MAC = "52:54:00:12:34:02"
SRC_MAC = "02:00:00:00:00:01"

SRC_IP = "10.0.0.1"
DST_IP = "10.0.0.2"

for src_port in (5000, 5001, 5002, 5003, 5004):
    packet = (
        Ether(src=SRC_MAC, dst=DST_MAC)
        / IP(src=SRC_IP, dst=DST_IP)
        / UDP(sport=src_port, dport=6000)
        / Raw(load=b"RSS experiment")
    )

    print(
        f"Sending UDP {SRC_IP}:{src_port}"
        f" -> {DST_IP}:6000"
    )

    sendp(packet, iface=IFACE, verbose=False)
