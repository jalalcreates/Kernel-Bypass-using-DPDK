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
#define BURST 32
#define RING_SIZE 1024
#define MEMPOOL_SIZE 8191
#define CACHE_SIZE 32
int configurePort( int argc, char *argv[]){
  int result = rte_eal_init(argc, argv);
  if(result <0)rte_exit(EXIT_FAILURE, "initialization error");
  argc -=result;argv+=result;
  
  struct rte_eth_dev_info devInfo{};
  int devInfoResult= rte_eth_dev_info_get(0,&devInfo);
  if(devInfoResult<0)rte_exit(EXIT_FAILURE, "could not get the NIC info");
  std::cout << "RSS key size: "<< static_cast<unsigned int>(devInfo.hash_key_size)<< '\n';
  std::cout << "RETA size: "<<devInfo.reta_size<<'\n';
  std::cout << "RSS flow types: 0x"<< std::hex << devInfo.flow_type_rss_offloads<<std::dec << '\n';

  struct rte_mempool *pool =rte_pktmbuf_pool_create("main pool",MEMPOOL_SIZE,CACHE_SIZE,0,RTE_MBUF_DEFAULT_BUF_SIZE,rte_eth_dev_socket_id(0));
  if(pool==nullptr)rte_exit(EXIT_FAILURE,"failed to create the mem pool");

  struct rte_eth_conf devConfig{};
  devConfig.rxmode.mq_mode = RTE_ETH_MQ_RX_RSS;
  devConfig.rxmode.mtu = 1500;
  devConfig.rxmode.offloads=RTE_ETH_RX_OFFLOAD_TCP_CKSUM;
  devConfig.txmode.mq_mode = RTE_ETH_MQ_TX_NONE;
  devConfig.rx_adv_conf.rss_conf.algorithm = RTE_ETH_HASH_FUNCTION_DEFAULT;
  devConfig.rx_adv_conf.rss_conf.rss_hf = RTE_ETH_RSS_NONFRAG_IPV4_UDP; 
  int dev = rte_eth_dev_configure(0, 3,3, &devConfig);
  if(dev<0){
    rte_exit(EXIT_FAILURE, "could not configure the dev");
  }
  
  for(int i=0; i<3; i++){
    int rx = rte_eth_rx_queue_setup(0,i,1024,rte_eth_dev_socket_id(0),nullptr,pool);
    if(rx<0)rte_exit(EXIT_FAILURE,"rx queue could not be set up");
    int tx = rte_eth_tx_queue_setup(0,i,1024,rte_eth_dev_socket_id(0),nullptr);
    if(tx<0)rte_exit(EXIT_FAILURE,"tx queue could not be set up");
  }


  int started =  rte_eth_dev_start(0);
  if(started <0)rte_exit(EXIT_FAILURE,"could not start the NIC");

  std::array<uint8_t,40> rssKey{};
  struct rte_eth_rss_conf rssConf{};
  rssConf.rss_key = rssKey.data();
  rssConf.rss_key_len= devInfo.hash_key_size;
  rssConf.rss_hf=0;
  int rssInfo=rte_eth_dev_rss_hash_conf_get(0, &rssConf);
  if(rssInfo<0) {
    std::cout<<"Could not get the hash information of the NIC";
    return 1;
  }
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
  int retaQueryResult = rte_eth_dev_rss_reta_query(0,retaQueryConf.data(),devInfo.reta_size);
  if(retaQueryResult<0)rte_exit(EXIT_FAILURE, "RETA query failed: %s\n", rte_strerror(-retaQueryResult));
  for(int group=0; group<retaGroups;group++){
    for(int index=0;index<RTE_ETH_RETA_GROUP_SIZE;index++){
      uint16_t entry = (group*RTE_ETH_RETA_GROUP_SIZE)+index;
      std::cout<<"RETA["<<entry<<"] = "<<retaQueryConf[group].reta[index]<<'\n';
    }
  } 
  return 0;
}
int main (int argc, char *argv[]) {
  return configurePort(argc, argv)  ;
}
