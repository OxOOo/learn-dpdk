## 内容

本节内容为实现一个TCP服务端，接受TCP连接并接收数据。

参考资料：
1. https://www.rfc-editor.org/rfc/rfc793
2. https://www.rfc-editor.org/rfc/rfc9293
4. https://cloud.tencent.com/developer/article/1781899

## 运行

首先执行`0_setup`中的内容，然后执行以下命令：

```sh
make run
```

然后在局域网内的另一台电脑上使用`nc`向DPDK服务器建立连接：

```
nc -v 192.168.86.178 8080
```

并向服务器发送一些数据(在`nc`命令中输入一些文字并回车)，在DPDK服务器上可以收到发送的内容：

```
move status to MAIN_LOOP
[ARP] Reply ARP Request
[TCPServer] Received SYN
[TCPServer] Accept new TCP connection from 192.168.86.155:46784 to 192.168.86.178:8080
[TASK] Created TCPConnection task
[TCP] Received data (length=12): hello world

[TCP] Relay ACK
[ARP] Reply ARP Request
[TCP] Received data (length=7): abcdef

[TCP] Relay ACK
[TCP] Received data (length=5): hehe

[TCP] Relay ACK
[TCP] Received data (length=7): xxcxzz

[TCP] Relay ACK
[TCP] Received data (length=3): ss

[TCP] Relay ACK
[TCP] Received data (length=3): dd

[TCP] Relay ACK
[TCP] Sent FIN
[TCP] Closed
[TASK] task TCPConnection ended
```

## 遇到的坑

无
