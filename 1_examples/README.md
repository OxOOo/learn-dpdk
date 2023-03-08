
## Hello World

```
sudo ./build/examples/dpdk-helloworld -c 3
```

主要功能：
1. 初始化EAL
2. 在每个core上启动一个函数

默认会使用所有核，可以使用-c参数指定只运行哪些核。

在EAL初始化之后，主线程会绑定一个主核，默认为第一个可用的核，可以使用参数--main-lcore指定主核。

## Basic Forwarding Sample Application

```
sudo ./build/examples/dpdk-skeleton
```

主要功能：将网口0收到的数据发往网口1,网口1收到的数据发往网口0，网口x的数据发往网口x xor 1。

### 初始化

`struct rte_mempool`初始化通过`rte_pktmbuf_pool_create`完成，DPDK所有发送和接收的网络数据包均为`struct rte_mbuf`。

在本example中，一个`mbuf`可能的生命周期如下：

1. 在main函数中使用`rte_pktmbuf_pool_create`被创建，此时`mbuf`在`mempool`中
2. 在初始化网卡的rx_ring时，被分配到一个rx descriptor，等待网卡数据写入
3. 当网卡收到数据，自动填充该`mbuf`，然后在调用`rte_eth_rx_burst`时被取出
    a. 当`rte_eth_rx_burst`从rx descriptor上取出被写入数据的`mbuf`时，同时会尝试从`mempool`申请一个新的`mbuf`对象和rc descriptor相关联
    b. 因此理论上rx descriptor始终有`mbuf`可以接收网卡的数据
    c. `mempool`在初始化网卡的rx ring的时候被指定
4. 从`rte_eth_rx_burst`取出的`mbuf`会尝试写入到另一张网卡的tx ring中(通过`rte_eth_tx_burst`函数)
    a. 如果tx ring中有空闲的tx descriptor，则将`mbuf`关联到该descriptor
    b. 如果该空闲tx descriptor之前就存在被使用过的`mbuf`对象，则该被使用过的`mbuf`会被调用`rte_pktmbuf_free`将其释放会`mempool`
5. 如果无法放到tx ring中，则会通过`rte_pktmbuf_free`将其放到`mempool`中，对外表现为该数据包丢失

可能的路径：
在初始mempool中 -> 被分配到一个rx descriptor -> 填充网卡的数据 -> 被`rte_eth_rx_burst`取出 -> 放到一个tx descriptor -> 等待网卡发送数据 -> 下一次要使用该tx descriptor时，被释放会mempool -> 某rx descriptor申请`mbuf`时，被重新分配 -> ...

`port_init`函数完成网卡的初始化，主要包括：设置rx ring和tx ring，设置rx descriptor和tx descriptor，关联mempool。

### 主循环

主循环较为简单，不断使用`rte_eth_rx_burst`接收网卡的数据，然后使用`rte_eth_tx_burst`从另一个网卡发送出去。
