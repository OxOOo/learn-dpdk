## 内容

本节内容为实现一个TCP客户端，向TCP server发起连接并定时发送数据，同时能够从server处接收数据。

由于TCP协议的拥塞控制算法特别复杂，因此我们这里只会实现最简单的停等协议，也就是发送一个数据包，等对方明确收到之后再发送第二个数据包(实际代码中根本没有考虑丢包)。

参考资料：
1. https://www.rfc-editor.org/rfc/rfc793
2. https://www.rfc-editor.org/rfc/rfc9293
4. https://cloud.tencent.com/developer/article/1781899

## 运行

本节的内容需要一个TCP server配合，因此先在局域网中的另一台服务器(我的实验环境中是一台IP地址为192.168.86.155的电脑)上运行：

```
nc -l 8080
```

然后执行`0_setup`中的内容，然后执行以下命令：

```sh
make run
```

如果运行正常的话，运行DPDK的电脑会输出如下内容：

```
move status to MAIN_LOOP
[ARP] MAC address for 192.168.86.155 is 2C:F0:5D:08:D3:6D
[TASK] task ARPRequest:192.168.86.155 ended
[TASK] Created TCP:192.168.86.155 task
[TCP] Sent SYN
[TCP] SYN,ACK Received
[TCP] Sent ACK
[TCP] Sent message
[ARP] Reply ARP Request
[TCP] Sent message
[ARP] Reply ARP Request
[TCP] Sent message
```

在IP地址为192.168.86.155的电脑上会每5秒收到`Hello DPDK TCP`消息。
我们也可以向DPDK发送消息，在运行`nc`命令的窗口输入`hello world`并回车，在运行DPDK的电脑上会收到该消息并输出：

```
[TCP] Received data (length=12): hello world

[TCP] Relay ACK
```

在IP地址为192.168.86.155的电脑退出`nc`命令窗口，从而关闭TCP连接，DPDK也能够完成TCP关闭的动作：

```
[TCP] Sent FIN
[TCP] Closed
[TASK] task TCP:192.168.86.155 ended
```

## 遇到的坑

使用TCP Checksum Offload时，需要先将`cksum`字段填充为伪IP头的Checksum才能正确计算出Offload Checksum。
如果Checksum是错误的，接收方会直接丢弃数据包并在`netstat -s`中`InCsumErrors`增加1。
