## 内容

本节我们要实现向局域网内的其他服务器发送UDP数据包的功能，UDP数据包本身的格式已经在上一节有所涉及，因此本节的重点并不是UDP数据包本身。
要向局域网内的其他服务器发送UDP数据包，除了要知道对方的IP地址之外，还需要知道对方的MAC地址，这个地方就涉及到ARP协议。

使用ARP协议获取到对方的MAC地址之后，我们会每隔1秒钟向对方发送一个UDP数据包，为了简便，这里使用了TSC计时，更科学的计时方式应该是使用DPDK的Timer(https://doc.dpdk.org/guides/sample_app_ug/timer.html 或 `1_examples`)。

参考资料：
1. https://zh.wikipedia.org/wiki/%E7%94%A8%E6%88%B7%E6%95%B0%E6%8D%AE%E6%8A%A5%E5%8D%8F%E8%AE%AE
2. https://www.rfc-editor.org/rfc/rfc6747

## 运行

需要先执行`0_setup`中的内容，然后执行以下命令：

```sh
make run
```

然后在另一台IP地址为`192.168.86.155`的电脑上运行：

```sh
nc -ul 8080
```

如果运行正常的话，运行DPDK的电脑会输出如下内容：

```
[TASK] Created ARPRequest:192.168.86.155 task
[ARP] Sent ARP request for 192.168.86.155
move status to MAIN_LOOP
[ARP] MAC address for 192.168.86.155 is 2C:F0:5D:08:D3:6D
[TASK] task ARPRequest:192.168.86.155 ended
[TASK] Created UDPSend:192.168.86.155 task
[UDP] Send UDP message from 192.168.86.178:8080 to 192.168.86.155:8080
[UDP] Send UDP message from 192.168.86.178:8080 to 192.168.86.155:8080
[UDP] Send UDP message from 192.168.86.178:8080 to 192.168.86.155:8080
[UDP] Send UDP message from 192.168.86.178:8080 to 192.168.86.155:8080
[UDP] Send UDP message from 192.168.86.178:8080 to 192.168.86.155:8080
[UDP] Send UDP message from 192.168.86.178:8080 to 192.168.86.155:8080
```

同时在`192.168.86.155`上可以每隔1秒钟收到一个`Hello DPDK`消息。

## 遇到的坑

无。
