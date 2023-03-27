## 内容

根据DHCP协议，向路由器请求一个IP地址。

DHCP协议说明：
1. https://zhuanlan.zhihu.com/p/265293856
2. https://www.rfc-editor.org/rfc/rfc2131
3. https://www.rfc-editor.org/rfc/rfc1533

## 运行

需要先执行`0_setup`中的内容，然后执行以下命令：

```sh
make run
```

如果运行正常的话，会输出如下内容：

```
move status to DHCP_START
move status to DHCP_DISCOVER_SENT
[DHCP] Got ip address 192.168.86.178 from 192.168.86.1 via DHCP
        [DHCP] Got netmask 255.255.255.0
        [DHCP] Got broadcast address 192.168.86.255
        [DHCP] Got gateway address 192.168.86.1
        [DHCP] Got DNS server address 192.168.86.1
move status to END
```

## 遇到的坑

我的网卡支持IP Checksum offload，也就是不需要我们使用CPU计算Checksum，而是交给网卡处理。
但是需要将ip_hdr->hdr_checksum设为0才能计算出正确的值。
