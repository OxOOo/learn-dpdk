## 内容

本节内容为实现一个TCP客户端，向TCP server发起连接并定时发送数据。

由于TCP协议的拥塞控制算法特别负责，因此我们这里只会实现最简单的停等协议，也就是发送一个数据包，等对方明确收到之后再发送第二个数据包。

## 运行

需要先执行`0_setup`中的内容，然后执行以下命令：

```sh
make run
```

我创建的阿里云服务器的IP地址为`39.107.102.23`，系统为Ubuntu 22.04，首先确认该服务器的安全组运行接收8080端口的UDP数据包，再确认服务器内的防火墙(如iptables)是关闭状态。
最后在`39.107.102.23`上运行：

```sh
nc -ul 8080
```

如果运行正常的话，运行DPDK的电脑会输出如下内容：

```
move status to MAIN_LOOP
[ARP] MAC address for 192.168.86.1 is 00:F0:CB:EF:DC:FA
[TASK] task ARPRequest:gateway ended
[TASK] Created UDPSend:39.107.102.23 task
[UDP] Send UDP message from 192.168.86.178:8080 to 39.107.102.23:8080
[UDP] Send UDP message from 192.168.86.178:8080 to 39.107.102.23:8080
[UDP] Send UDP message from 192.168.86.178:8080 to 39.107.102.23:8080
[UDP] Send UDP message from 192.168.86.178:8080 to 39.107.102.23:8080
[UDP] Send UDP message from 192.168.86.178:8080 to 39.107.102.23:8080
[UDP] Send UDP message from 192.168.86.178:8080 to 39.107.102.23:8080
```

同时在阿里云服务器上可以每隔1秒钟收到一个`Hello DPDK`消息。

## 遇到的坑

无。
