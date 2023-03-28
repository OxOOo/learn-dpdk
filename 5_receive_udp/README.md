## 内容

接收其他服务器发来的UDP数据包。

参考资料：
1. https://zh.wikipedia.org/wiki/%E7%94%A8%E6%88%B7%E6%95%B0%E6%8D%AE%E6%8A%A5%E5%8D%8F%E8%AE%AE

## 运行

需要先执行`0_setup`中的内容，然后执行以下命令：

```sh
make run
```

然后在另一台电脑上运行：

```sh
echo "Hello UDP" | nc -u 192.168.86.178 8080
```

如果运行正常的话，运行DPDK的电脑会输出如下内容：

```
TASK] Created ARPReply task
[TASK] Created PingReply task
[TASK] Created UDP:8080 task
move status to MAIN_LOOP
[ARP] Reply ARP Request
[UDP] Received from 192.168.86.155:46958 to 192.168.86.178:8080 with message = `Hello UDP
`
[ARP] Reply ARP Request
[UDP] Received from 192.168.86.155:44673 to 192.168.86.178:8080 with message = `Hello UDP
`
[UDP] Received from 192.168.86.155:56285 to 192.168.86.178:8080 with message = `Hello UDP
`
```

## 遇到的坑

无。
