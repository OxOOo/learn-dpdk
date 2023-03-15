
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

TODO：按照文档说明，DPDK好像会自动将多个帧合并为一个`mbuf`？然后自动在发送的时候拆分？

## Network Layer 2 forwarding

这个示例和上一个示例差不多，也是将port 0收到的数据转发到port 1，因此也可以完成`0_setup`中的示例（模拟网线的功能）：

```
sudo ./build/examples/dpdk-l2fwd -- -p 3 -P --no-mac-updating
```

相比上一个示例，增加了以下功能：
1. -P混杂模式(`promiscuous mode`)：在上个示例该选项是默认打开的，而在本选项中默认是关闭的。当混杂模式关闭的情况下，网卡会自动检查收到数据包的dst mac，如果dst mac和网卡的mac不匹配，就会直接将数据包丢弃。
2. --[no-]mac-updating：在启动mac updating的情况下，转发的数据包的src mac会被替换为发出数据包的mac，dst mac会被替换为02:00:00:00:00:TX_PORT_ID。

所以在启用mac updating(默认启用)的情况下，是不能够完成模拟网线的功能的，但是如果修改代码，将修改dst mac的功能去掉，只保留修改src mac的功能，则依然能够完成模拟网线的功能。
注释`l2fwd_mac_updating`中修改dst mac的代码，并运行以下命令：
```
sudo ./build/examples/dpdk-l2fwd -- -p 3 -P
```

### 知识点一：多线程处理数据
这个实例还启动了多个线程处理数据，每个网口的接收会被分配给一个线程，每个线程默认负责1个网口，可以使用`-q`参数修改每个线程负责的网口数量。
但是全局只有一个`mbuf pool`，也就是说mempool相关的操作(alloc和free)是线程安全的，经查证确实如此： https://stackoverflow.com/questions/68068452/are-the-dpdk-functions-rte-pktmbuf-alloc-and-rte-pktmbuf-free-thread-safe

### 知识点二：`tx_buffer`
为了提高性能，这个示例中从一个网口收到需要转发的数据之后，并不会马上发给另一个网口，而是先放到一个`tx_buffer`中，等到`tx_buffer`中积累的足够的数据，或者是100us之后，才会发送。
`tx_buffer`内部就是一个简单的数组，和网口不是绑定的关系。
TODO：相比直接使用`rte_eth_tx_burst`的提升在哪？

### 知识点三：检查网口状态
可以获取网卡是否启动(是否有网线连接)，网速等信息。
由于在检查网卡都启动之后，马上就切换到了统计信息页面，所以看不到输出内容。可以在`check_all_ports_link_status`函数之后调用`rte_delay_ms(5000)`，输出的内容类似：`Port 0 Link up at 1 Gbps FDX Autoneg`。

### 知识点四：`rte_prefetch0`
预取指令，可以预取一条cache line到cache中，`rte_prefetch0`就是预取到所有层级的cache中。
预取指令是一条CPU指令，预取是异步的，并不需要等到预取结束才执行之后的命令，所以理论上能够提升性能。
参考：https://blog.csdn.net/cling60/article/details/78480725

## L2 Forwarding Eventdev Sample Application

和上一个示例差不多，所以一样可以完成模拟网线的功能，只不过多了一种eventdev的模式。不过我的网卡不支持eventdev，所以只能使用poll模式：

```
sudo ./build/examples/dpdk-l2fwd -- -p 3 -P --no-mac-updating --mode=poll
```

TODO：什么是eventdev。
参考：https://zhuanlan.zhihu.com/p/27990594
参考：https://learn.microsoft.com/en-us/windows-hardware/drivers/network/rss-with-hardware-queuing

## Distributor Sample Application

本示例展示了如何使用多个worker协同处理数据。
一个收到的网络数据包的处理路径：
1. `lcore_rx`:`rte_eth_rx_burst`从网卡收到数据包
2. `lcore_rx`:`rte_ring_enqueue_burst`将数据包发送到`rx_dist_ring`
3. `lcore_distributor`:`rte_ring_dequeue_burst`从`rx_dist_ring`取出数据
4. `lcore_distributor`:`rte_distributor_process`将数据交给worker线程进行处理
4. `lcore_worker`:`rte_distributor_get_pkt`将数据从distributor取出需要处理的数据包，并且将处理完的数据包交回distributor
5. `lcore_distributor`:`rte_distributor_returned_pkts`取回worker线程处理完成的数据
6. `lcore_distributor`:`rte_ring_enqueue_burst`将数据放入`dist_tx_ring`
7. `lcore_tx`:`rte_ring_dequeue_burst`将数据从`dist_tx_ring`中取出
8. `lcore_tx`:`flush_one_port`/`flush_all_ports`将数据从网卡发送出去

在`lcore_rx`:`rte_ring_enqueue_burst`出也可以直接将数据包发送到`dist_tx_ring`(代码注释的部分)，这样数据就不经过处理直接输出。

TODO：在`lcore_distributor`中，使用`rte_distributor_process`分发数据之后马上就使用`rte_distributor_returned_pkts`接收处理完成的数据了，这是数据应该还没有加工完成才对？
TODO：distributor似乎是按照flow id或tag进行分配的？

### 知识点：配置RSS
初始化的时候设置了：
```C++
.rx_adv_conf = {
    .rss_conf = {
        .rss_hf = RTE_ETH_RSS_IP | RTE_ETH_RSS_UDP |
            RTE_ETH_RSS_TCP | RTE_ETH_RSS_SCTP,
    }
}
```
并根据网卡支持的功能进行调整：
```C++
port_conf.rx_adv_conf.rss_conf.rss_hf &=
		dev_info.flow_type_rss_offloads
```

### 知识点：`rte_distributor`
使用`rte_distributor_create`创建一个distributor。
在每个worker线程，可以使用`rte_distributor_get_pkt`函数获取到需要处理的数据`mbuf`，同时将处理结束的`mbuf`返回。

注意事项：worker不能缓存`mbuf`。

参考资料：
1. https://doc.dpdk.org/guides/prog_guide/packet_distrib_lib.html

### 知识点：`rte_ring`
`rte_ring`是一个固定长度的循环队列，并且非链表，而是直接将数据储存在一块预先分配的内存空间中。
在本例中，`rte_ring`存的是`mbuf`的指针。

使用`rte_ring_create`创建一个`rte_ring`，在创建是可以指定flag，表明生产者的特征：
1. 单生成者
2. 多生产者模式
2. 多生产者RTS模式
3. 多生产者HTS模式
`flag`设为0就是多生产者模式，其他选项分别对应另外3种模式，消费端也有类似的4个选项。

参考资料：
1. https://doc.dpdk.org/guides/prog_guide/ring_lib.html
2. https://zhuanlan.zhihu.com/p/515046756

以入队为例，分为3个阶段：
1. 在循环队列中圈出一块空间，这个阶段需要保证多生成者同时进行入队操作时能够分配到不同的空间
2. 将数据拷贝到循环队列中
3. 修改`rte_ring`的变量告知入队操作结束，类似commit

如果是单生产者模式，则上面3个操作不需要同步。
如果是多生产者模式(或RTS/HTS)，则第一个操作需要使用CAS原子操作避免冲突，第2个阶段可以并发写入。第3个阶段同样需要使用CAS原子操作避免冲突。
在第3个阶段，多生产者模式和RTS模式分别有不同的处理逻辑，前者要求commit操作依次完成，后面生产者的commit必须等待前面生产者的commit完成，因此理论上这个存在一个自旋锁。
而RTS模式则可以由最后一个生产者进行commit即可。
HTS则是要求所有生产者都串行执行，也就是说如果一个生产者A在第2阶段拷贝数据，那么其他生成则必须等待生产者A完成第3阶段才能从第1阶段开始执行。因此HTS是严格串行的。
