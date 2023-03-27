## 内容

本节整理现有的代码，并实现对ping的响应。

参考资料：
1. https://www.rfc-editor.org/rfc/rfc792

## 运行

需要先执行`0_setup`中的内容，然后执行以下命令：

```sh
make run
```

然后在另一台电脑上运行：

```sh
ping 192.168.86.178
```

如果运行正常的话，运行DPDK的电脑会输出如下内容：

```
[TASK] Created ARPReply task
[TASK] Created PingReply task
move status to MAIN_LOOP
[ARP] Reply ARP Request
[ARP] Reply ARP Request
[PING] Reply Ping Request
[PING] Reply Ping Request
[PING] Reply Ping Request
[PING] Reply Ping Request
[PING] Reply Ping Request
```

另一台电脑上会输出：

```
PING 192.168.86.178 (192.168.86.178) 56(84) bytes of data.
64 bytes from 192.168.86.178: icmp_seq=1 ttl=64 time=0.782 ms
64 bytes from 192.168.86.178: icmp_seq=2 ttl=64 time=0.175 ms
64 bytes from 192.168.86.178: icmp_seq=3 ttl=64 time=0.182 ms
64 bytes from 192.168.86.178: icmp_seq=4 ttl=64 time=0.202 ms
64 bytes from 192.168.86.178: icmp_seq=5 ttl=64 time=0.166 ms
64 bytes from 192.168.86.178: icmp_seq=6 ttl=64 time=0.162 ms
64 bytes from 192.168.86.178: icmp_seq=7 ttl=64 time=0.171 ms
```

## 遇到的坑

Header Checksum只需要计算header部分的Checksum，但是ICMP Reply需要计算Header+Payload一起的Checksum。
