#include<iostream>
#include<rte_eal.h>
#include<rte_lcore.h>
#include<rte_mempool.h>
#include<rte_mbuf.h>
#include <cstdint>
#include <array>
#include <rte_ethdev.h>
#include <rte_errno.h>
#include <iomanip>
#include <vector>
#include <rte_ether.h>
#include<rte_ip.h>
#include<rte_udp.h>
#include <rte_byteorder.h>
#include <rte_thash.h>
#include <cstring>
#include <rte_ring.h>
#include<atomic>
#define BURST 32
#define RING_SIZE 1024
#define MEMPOOL_SIZE 8191
#define CACHE_SIZE 32

void configurePort( int argc, char *argv[], uint16_t portId, uint16_t nbRX, uint16_t nbTX);
void inspectPort(uint16_t portId);
int worker(void* arg);
void printPacket(rte_mbuf* m, uint16_t queueId, uint16_t index);
void printMAC(const rte_ether_addr& mac);
void printIPv4(uint32_t address);
void pollQueues(uint16_t portId, uint16_t nbQueues, rte_ring* rings[]);

struct workerStats{
    std::atomic<uint64_t> processed{0};
    std::atomic<uint64_t> ipv4Packets{0};
    std::atomic<uint64_t> udpPackets{0};
    std::atomic<uint64_t> udpPayloadBytes{0};
    std::atomic<uint64_t> smallPackets{0};
    std::atomic<uint64_t> largePackets{0};
};
std::array<workerStats, 3> stats{};
struct workerSpecifics{
    rte_ring* ring;
    uint16_t queueId;
};
std::array<std::atomic<uint64_t>, 3> workerCounts{};
std::vector<uint16_t> retaEntries;
std::array<uint32_t, 10> convertedRssKeyWords{};
int main(int argc, char *argv[]){
    uint16_t portId=0;

    configurePort(argc,argv,portId,3,3);
    inspectPort(portId);

    rte_ring* rings[3];

    const char* ringNames[3] = {
        "ring_0",
        "ring_1",
        "ring_2"
    };

    for(uint16_t i=0; i<3; i++){
        rings[i]=rte_ring_create(
            ringNames[i],
            RING_SIZE,
            rte_socket_id(),
            RING_F_SP_ENQ | RING_F_SC_DEQ
        );

        if(rings[i]==nullptr)
            rte_exit(EXIT_FAILURE,"failed to create ring %u\n",i);
    }

    workerSpecifics workerArgs[3];
    uint32_t coreId;
    uint16_t queueId=0;

    RTE_LCORE_FOREACH_WORKER(coreId){
        if(queueId>=3)
            break;

        workerArgs[queueId]={
            rings[queueId],
            queueId
        };

        int launched=rte_eal_remote_launch(
            worker,
            &workerArgs[queueId],
            coreId
        );

        if(launched<0)
            rte_exit(EXIT_FAILURE,"failed to launch worker cores %s\n",rte_strerror(-launched));

        queueId++;
    }

    if(queueId!=3)
        rte_exit(EXIT_FAILURE,"three worker lcores are required\n");

    pollQueues(portId,3,rings);

    return 0;
}
void configurePort( int argc, char *argv[], uint16_t portId, uint16_t nbRX, uint16_t nbTX){
  int result = rte_eal_init(argc, argv);
  if(result <0)rte_exit(EXIT_FAILURE, "initialization error");
  argc -=result;argv+=result;

  struct rte_mempool *pool =rte_pktmbuf_pool_create("main pool",MEMPOOL_SIZE,CACHE_SIZE,0,RTE_MBUF_DEFAULT_BUF_SIZE,rte_eth_dev_socket_id(0));
  if(pool==nullptr)rte_exit(EXIT_FAILURE,"failed to create the mem pool");

  struct rte_eth_conf devConfig{};
  devConfig.rxmode.mq_mode = RTE_ETH_MQ_RX_RSS;
  devConfig.rxmode.mtu = 1500;
  devConfig.rxmode.offloads=RTE_ETH_RX_OFFLOAD_TCP_CKSUM;
  devConfig.txmode.mq_mode = RTE_ETH_MQ_TX_NONE;
  devConfig.rx_adv_conf.rss_conf.algorithm = RTE_ETH_HASH_FUNCTION_DEFAULT;
  devConfig.rx_adv_conf.rss_conf.rss_hf = RTE_ETH_RSS_NONFRAG_IPV4_UDP; 
  int dev = rte_eth_dev_configure(portId, nbRX,nbTX, &devConfig);
  if(dev<0){
    rte_exit(EXIT_FAILURE, "could not configure the dev");
  }
  
  for(int i=0; i<3; i++){
    int rx = rte_eth_rx_queue_setup(portId,i,1024,rte_eth_dev_socket_id(0),nullptr,pool);
    if(rx<0)rte_exit(EXIT_FAILURE,"rx queue could not be set up");
    int tx = rte_eth_tx_queue_setup(portId,i,1024,rte_eth_dev_socket_id(0),nullptr);
    if(tx<0)rte_exit(EXIT_FAILURE,"tx queue could not be set up");
  }


  int started =  rte_eth_dev_start(portId);
  if(started <0)rte_exit(EXIT_FAILURE,"could not start the NIC");

    return;
}

void inspectPort(uint16_t portId){
  
  struct rte_eth_dev_info devInfo{};
  int devInfoResult= rte_eth_dev_info_get(portId,&devInfo);
  if(devInfoResult<0)rte_exit(EXIT_FAILURE, "could not get the NIC info");
  std::cout << "RSS key size: "<< static_cast<unsigned int>(devInfo.hash_key_size)<< '\n';
  std::cout << "RETA size: "<<devInfo.reta_size<<'\n';
  std::cout << "RSS flow types: 0x"<< std::hex << devInfo.flow_type_rss_offloads<<std::dec << '\n';

std::vector<uint8_t> rssKey(devInfo.hash_key_size);
  struct rte_eth_rss_conf rssConf{};
  rssConf.rss_key = rssKey.data();
  rssConf.rss_key_len= devInfo.hash_key_size;
  rssConf.rss_hf=0;
  int rssInfo=rte_eth_dev_rss_hash_conf_get(portId, &rssConf);
  if(rssInfo<0) {
    std::cout<<"Could not get the hash information of the NIC";
    return;
  }
  std::array<uint32_t, 10> originalRssKeyWords{};
  std::memcpy(originalRssKeyWords.data(),rssKey.data(),rssKey.size());
  rte_convert_rss_key(originalRssKeyWords.data(),convertedRssKeyWords.data(),rssKey.size());
  std::cout<<"RSS query result: " << rssInfo << '\n';
  std::cout<<"Configured RSS types: 0x"<<std::hex<<rssConf.rss_hf<<std::dec<<'\n';
  std::cout<<"RSS key: ";
  for(uint8_t byte:rssKey) {
      std::cout<<std::hex<<std::setw(2)<<std::setfill('0')<<static_cast<unsigned int>(byte)<<' ';
  }
  std::cout<<std::dec<<'\n';

  int retaGroups=devInfo.reta_size/RTE_ETH_RETA_GROUP_SIZE;
  std::vector<struct rte_eth_rss_reta_entry64> retaQueryConf(retaGroups);
  for(int i=0; i<retaGroups; i++){
    retaQueryConf[i].mask = UINT64_MAX;
  }
  int retaQueryResult = rte_eth_dev_rss_reta_query(portId,retaQueryConf.data(),devInfo.reta_size);
  if(retaQueryResult<0)rte_exit(EXIT_FAILURE, "RETA query failed: %s\n", rte_strerror(-retaQueryResult));
  retaEntries.resize(devInfo.reta_size);

for (int group = 0; group < retaGroups; group++) {
    for (int index = 0; index < RTE_ETH_RETA_GROUP_SIZE; index++) {
        uint16_t entry = group * RTE_ETH_RETA_GROUP_SIZE + index;

        if (entry < devInfo.reta_size) {
            retaEntries[entry] = retaQueryConf[group].reta[index];
        }
    }
}


  return;
}
void pollQueues(uint16_t portId, uint16_t nbQueues, rte_ring* rings[]){
    std::array<rte_mbuf*, BURST> bufs{};
    std::array<void*, BURST> objects{};
    std::array<uint64_t, 3> packetCounts{};

    while(true){
        for(uint16_t queueId=0; queueId<nbQueues; queueId++){
            uint16_t rxPackets=rte_eth_rx_burst(
                portId,
                queueId,
                bufs.data(),
                BURST
            );

            packetCounts[queueId]+=rxPackets;

            for(uint16_t i=0; i<rxPackets; i++){
                objects[i]=bufs[i];
            }

            unsigned int enqueued=rte_ring_enqueue_burst(
                rings[queueId],
                objects.data(),
                rxPackets,
                nullptr
            );

            for(unsigned int i=enqueued; i<rxPackets; i++){
                rte_pktmbuf_free(bufs[i]);
            }
        }

        static uint64_t iterations=0;
        iterations++;

       if(iterations % 1000000 == 0){
        std::cout
            << "RX: "
            << packetCounts[0] << " | "
            << packetCounts[1] << " | "
            << packetCounts[2]
            << " || WORKERS: "
            << workerCounts[0].load() << " | "
            << workerCounts[1].load() << " | "
            << workerCounts[2]
            << '\n';

        std::cout
            << "W0 IPv4: "
            << stats[0].ipv4Packets.load()
            << " || W1 UDP: "
            << stats[1].udpPackets.load()
            << " | UDP payload bytes: "
            << stats[1].udpPayloadBytes.load()
            << " || W2 small: "
            << stats[2].smallPackets.load()
            << " | large: "
            << stats[2].largePackets.load()
            << '\n';
}    }
}
struct packetInfo{
    bool ipv4 = false;
    bool udp = false;
    uint16_t udpPayloadBytes = 0;
};

bool inspectPacket(rte_mbuf* m, packetInfo& info){
    if(m->pkt_len < sizeof(rte_ether_hdr))
        return false;

    rte_ether_hdr ethStorage{};
    auto* eth = reinterpret_cast<const rte_ether_hdr*>(
        rte_pktmbuf_read(m, 0, sizeof(ethStorage), &ethStorage)
    );

    if(!eth || rte_be_to_cpu_16(eth->ether_type) != RTE_ETHER_TYPE_IPV4)
        return true;

    info.ipv4 = true;

    constexpr uint32_t ipOffset = sizeof(rte_ether_hdr);

    if(m->pkt_len < ipOffset + sizeof(rte_ipv4_hdr))
        return true;

    rte_ipv4_hdr ipStorage{};
    auto* ip = reinterpret_cast<const rte_ipv4_hdr*>(
        rte_pktmbuf_read(m, ipOffset, sizeof(ipStorage), &ipStorage)
    );

    if(!ip)
        return true;

    uint16_t ipHeaderLength = (ip->version_ihl & 0x0f) * 4;

    if(ipHeaderLength < sizeof(rte_ipv4_hdr))
        return true;

    if(ip->next_proto_id != IPPROTO_UDP)
        return true;

    uint16_t ipTotalLength = rte_be_to_cpu_16(ip->total_length);

    if(ipTotalLength < ipHeaderLength + sizeof(rte_udp_hdr))
        return true;

    uint32_t udpOffset = ipOffset + ipHeaderLength;

    if(m->pkt_len < udpOffset + sizeof(rte_udp_hdr))
        return true;

    rte_udp_hdr udpStorage{};
    auto* udp = reinterpret_cast<const rte_udp_hdr*>(
        rte_pktmbuf_read(m, udpOffset, sizeof(udpStorage), &udpStorage)
    );

    if(!udp)
        return true;

    uint16_t udpLength = rte_be_to_cpu_16(udp->dgram_len);

    if(udpLength < sizeof(rte_udp_hdr) ||
       udpLength > ipTotalLength - ipHeaderLength)
        return true;

    info.udp = true;
    info.udpPayloadBytes = udpLength - sizeof(rte_udp_hdr);

    return true;
}
int worker(void* arg){
    workerSpecifics* data = static_cast<workerSpecifics*>(arg);
    std::array<void*, BURST> bufs{};

    while(true){
        unsigned int received = rte_ring_dequeue_burst(
            data->ring,
            bufs.data(),
            BURST,
            nullptr
        );

        for(unsigned int i = 0; i < received; i++){
            auto* m = static_cast<rte_mbuf*>(bufs[i]);

            if(data->queueId == 0){
                packetInfo info{};

                if(inspectPacket(m, info) && info.ipv4)
                    stats[0].ipv4Packets++;
            }
            else if(data->queueId == 1){
                packetInfo info{};

                if(inspectPacket(m, info) && info.udp){
                    stats[1].udpPackets++;
                    stats[1].udpPayloadBytes += info.udpPayloadBytes;
                }
            }
            else if(data->queueId == 2){
                if(m->pkt_len <= 128)
                    stats[2].smallPackets++;
                else
                    stats[2].largePackets++;
            }

            stats[data->queueId].processed++;
            rte_pktmbuf_free(m);
        }

        workerCounts[data->queueId] += received;
    }

    return 0;
}
void printMAC(const rte_ether_addr& mac){
  for(int i=0;i<RTE_ETHER_ADDR_LEN;i++){
    if(i!=0)std::cout<<':';
    std::cout<<std::hex<<std::setw(2)<<std::setfill('0')
             <<static_cast<unsigned int>(mac.addr_bytes[i]);
  }
  std::cout<<std::dec;
}

void printIPv4(uint32_t address){
  uint32_t ip=rte_be_to_cpu_32(address);
  std::cout<<((ip>>24)&0xff)<<'.'
           <<((ip>>16)&0xff)<<'.'
           <<((ip>>8)&0xff)<<'.'
           <<(ip&0xff);
}

void printPacket(rte_mbuf* m,uint16_t queueId,uint16_t index){
  std::cout<<"\n========== RX PACKET ==========\n"
           <<"RX queue: "<<queueId<<'\n'
           <<"Packet index in burst: "<<index<<'\n'
           <<"Packet length: "<<m->pkt_len<<" bytes\n"
           <<"First segment length: "<<m->data_len<<" bytes\n"
           <<"Number of segments: "<<static_cast<unsigned>(m->nb_segs)<<'\n'
           <<"Data offset: "<<m->data_off<<'\n'
           <<"Offload flags: 0x"<<std::hex<<m->ol_flags<<'\n'
           <<"Packet type: 0x"<<m->packet_type<<std::dec<<'\n'
           <<"Port: "<<m->port<<'\n';

if(m->ol_flags&RTE_MBUF_F_RX_RSS_HASH){
  uint32_t rssHash=m->hash.rss;
  std::cout<<"RSS hash: 0x"<<std::hex<<rssHash
           <<std::dec<<" ("<<rssHash<<")\n";

  if(!retaEntries.empty()){
    uint16_t retaIndex=rssHash&(retaEntries.size()-1);
    uint16_t expectedQueue=retaEntries[retaIndex];

    std::cout<<"RETA index: "<<retaIndex<<'\n'
             <<"Expected RX queue: "<<expectedQueue<<'\n'
             <<"Actual RX queue: "<<queueId<<'\n'
             <<"RSS queue match: "
             <<(expectedQueue==queueId?"YES":"NO")<<'\n';
  }
}else{
  std::cout<<"RSS hash: unavailable\n";
}
  if(m->data_len<sizeof(rte_ether_hdr)){
    std::cout<<"Ethernet header incomplete\n";
    return;
  }

  rte_ether_hdr* eth=rte_pktmbuf_mtod(m,rte_ether_hdr*);
  uint16_t etherType=rte_be_to_cpu_16(eth->ether_type);

  std::cout<<"\n--- Ethernet header ---\nDestination MAC: ";
  printMAC(eth->dst_addr);
  std::cout<<"\nSource MAC: ";
  printMAC(eth->src_addr);
  std::cout<<"\nEtherType: 0x"<<std::hex<<etherType<<std::dec<<'\n';

  if(etherType!=RTE_ETHER_TYPE_IPV4){
    std::cout<<"IPv4 decoding skipped\n";
    return;
  }

  uint16_t ethLen=sizeof(rte_ether_hdr);
  if(m->data_len<ethLen+sizeof(rte_ipv4_hdr)){
    std::cout<<"IPv4 header incomplete\n";
    return;
  }

  rte_ipv4_hdr* ip=rte_pktmbuf_mtod_offset(m,rte_ipv4_hdr*,ethLen);
  uint8_t version=ip->version_ihl>>4;
  uint8_t ihl=(ip->version_ihl&0x0f)*4;
  uint16_t fragment=rte_be_to_cpu_16(ip->fragment_offset);

  std::cout<<"\n--- IPv4 header ---\n"
           <<"Version: "<<static_cast<unsigned>(version)<<'\n'
           <<"Header length: "<<static_cast<unsigned>(ihl)<<" bytes\n"
           <<"Type of service: 0x"<<std::hex
           <<static_cast<unsigned>(ip->type_of_service)<<std::dec<<'\n'
           <<"Total length: "<<rte_be_to_cpu_16(ip->total_length)<<'\n'
           <<"Identification: "<<rte_be_to_cpu_16(ip->packet_id)<<'\n'
           <<"Flags: 0x"<<std::hex<<((fragment>>13)&7)<<std::dec<<'\n'
           <<"Fragment offset: "<<(fragment&0x1fff)<<'\n'
           <<"TTL: "<<static_cast<unsigned>(ip->time_to_live)<<'\n'
           <<"Protocol: "<<static_cast<unsigned>(ip->next_proto_id)<<'\n'
           <<"Header checksum: 0x"<<std::hex
           <<rte_be_to_cpu_16(ip->hdr_checksum)<<std::dec<<'\n'
           <<"Source IP: ";
  printIPv4(ip->src_addr);
  std::cout<<"\nDestination IP: ";
  printIPv4(ip->dst_addr);
  std::cout<<'\n';

  if(ihl<sizeof(rte_ipv4_hdr)||m->data_len<ethLen+ihl){
    std::cout<<"Invalid or incomplete IPv4 header length\n";
    return;
  }

  if(fragment&0x3fff){
    std::cout<<"Fragmented IPv4 packet; transport decoding skipped\n";
    return;
  }

  uint16_t l4Offset=ethLen+ihl;

  if(ip->next_proto_id==IPPROTO_UDP){
    if(m->data_len<l4Offset+sizeof(rte_udp_hdr)){
      std::cout<<"UDP header incomplete\n";
      return;
    }

    rte_udp_hdr* udp=rte_pktmbuf_mtod_offset(m,rte_udp_hdr*,l4Offset);
    std::cout<<"\n--- UDP header ---\n"
             <<"Source port: "<<rte_be_to_cpu_16(udp->src_port)<<'\n'
             <<"Destination port: "<<rte_be_to_cpu_16(udp->dst_port)<<'\n'
             <<"UDP length: "<<rte_be_to_cpu_16(udp->dgram_len)<<'\n'
             <<"UDP checksum: 0x"<<std::hex
             <<rte_be_to_cpu_16(udp->dgram_cksum)<<std::dec<<'\n';
    if(m->ol_flags&RTE_MBUF_F_RX_RSS_HASH){
        rte_ipv4_tuple tuple{};
        tuple.src_addr=rte_be_to_cpu_32(ip->src_addr);
        tuple.dst_addr=rte_be_to_cpu_32(ip->dst_addr);
        tuple.sport=rte_be_to_cpu_16(udp->src_port);
        tuple.dport=rte_be_to_cpu_16(udp->dst_port);
        uint32_t softwareHash=rte_softrss_be( reinterpret_cast<uint32_t*>(&tuple),RTE_THASH_V4_L4_LEN,reinterpret_cast<const uint8_t*>(convertedRssKeyWords.data()));
        std::cout<<"Software RSS hash: 0x"<<std::hex<<softwareHash<<'\n'
                <<"Hardware RSS hash: 0x"<<m->hash.rss<<'\n'
                <<"Hash match: "<<(softwareHash==m->hash.rss?"YES":"NO")
                <<std::dec<<'\n';
    }
  }
  else if(ip->next_proto_id==IPPROTO_TCP){
    if(m->data_len<l4Offset+sizeof(rte_tcp_hdr)){
      std::cout<<"TCP header incomplete\n";
      return;
    }

    rte_tcp_hdr* tcp=rte_pktmbuf_mtod_offset(m,rte_tcp_hdr*,l4Offset);
    uint8_t tcpLen=(tcp->data_off>>4)*4;
    uint8_t flags=tcp->tcp_flags;

    std::cout<<"\n--- TCP header ---\n"
             <<"Source port: "<<rte_be_to_cpu_16(tcp->src_port)<<'\n'
             <<"Destination port: "<<rte_be_to_cpu_16(tcp->dst_port)<<'\n'
             <<"Sequence number: "<<rte_be_to_cpu_32(tcp->sent_seq)<<'\n'
             <<"Acknowledgment number: "<<rte_be_to_cpu_32(tcp->recv_ack)<<'\n'
             <<"Header length: "<<static_cast<unsigned>(tcpLen)<<" bytes\n"
             <<"Flags: 0x"<<std::hex<<static_cast<unsigned>(flags)<<std::dec<<'\n'
             <<"Window size: "<<rte_be_to_cpu_16(tcp->rx_win)<<'\n'
             <<"TCP checksum: 0x"<<std::hex<<rte_be_to_cpu_16(tcp->cksum)
             <<std::dec<<'\n'
             <<"Urgent pointer: "<<rte_be_to_cpu_16(tcp->tcp_urp)<<'\n';

    if(tcpLen<sizeof(rte_tcp_hdr)||m->data_len<l4Offset+tcpLen)
      std::cout<<"TCP options area incomplete or invalid\n";
    else if(tcpLen>sizeof(rte_tcp_hdr))
      std::cout<<"TCP options length: "<<tcpLen-sizeof(rte_tcp_hdr)<<" bytes\n";
  }
   std::cout<<"========== END PACKET ==========\n";
}
