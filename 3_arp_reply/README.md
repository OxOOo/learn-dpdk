## 内容

上一节已经能够从路由器获取到IP地址了，但是其他服务器还不能和我们进行通信。
其他服务器如果想向我们发送IP数据包，除了我们的IP地址之外，还需要知道我们的MAC地址，而根据IP地址获取到MAC地址的协议是ARP协议。
如果某个服务器IP_A想发送一个UDP数据包给我们的服务器IP_B，完整的流程应该是这样的：
1. IP_A向局域网**广播**一个ARP Request，询问IP_B的MAC地址。
2. 局域网中其他服务器都会收到该请求，但只有IP_B发现上一步中请求的IP地址和自己的地址匹配，因此构造一个ARP Reply数据包，其中包含自己的MAC地址，发送给IP_A。
3. IP_A收到IP_B发送的ARP Reply，因此知道了IP_B的MAC地址。
4. IP_A利用IP_B的IP地址和MAC地址，就能够向IP_B发送UDP数据包了。

**本节的主要内容就是实现第2步中IP_B回应ARP Request的功能。**

参考资料：
1. https://www.rfc-editor.org/rfc/rfc6747

## 运行

需要先执行`0_setup`中的内容，然后执行以下命令：

```sh
make run
```

然后在另一台电脑上运行：

```sh
sudo arping 192.168.86.178
```

如果运行正常的话，运行DPDK的电脑会输出如下内容：

```
move status to MAIN_LOOP
[ARP] Reply ARP Request
[ARP] Reply ARP Request
[ARP] Reply ARP Request
```

另一台电脑上会输出：

```
60 bytes from a0:36:9f:0f:bc:9c (192.168.86.178): index=0 time=553.590 usec
60 bytes from a0:36:9f:0f:bc:9c (192.168.86.178): index=1 time=1.039 msec
60 bytes from a0:36:9f:0f:bc:9c (192.168.86.178): index=2 time=470.801 usec
60 bytes from a0:36:9f:0f:bc:9c (192.168.86.178): index=3 time=1.171 msec
60 bytes from a0:36:9f:0f:bc:9c (192.168.86.178): index=4 time=504.240 usec
60 bytes from a0:36:9f:0f:bc:9c (192.168.86.178): index=5 time=558.581 usec
60 bytes from a0:36:9f:0f:bc:9c (192.168.86.178): index=6 time=959.881 usec
```

## 遇到的坑

记得正确设置`pkt_len`。
