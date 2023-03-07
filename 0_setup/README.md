
# 网卡

Intel E1G42ET，芯片JL82576。

# 系统

Ubuntu 22.04

修改`/etc/default/grub`，给`GRUB_CMDLINE_LINUX_DEFAULT`添加`intel_iommu=on iommu=pt`参数，然后运行`sudo grub-mkconfig -o /boot/grub/grub.cfg`。

重启系统。

在BIOS中开启VT-d选项(MSI的主板在"高级模式"->"OC"->"CPU特征"中)。

启动系统，输入`sudo dmesg | grep -e DMAR -e IOMMU`查看是否开启，不同系统可能不一样，我的输出是：
```
DMAR: IOMMU enabled
```

## 调试

参考https://doc.dpdk.org/guides/linux_gsg/linux_drivers.html#vfio 7.2.4。
如果在绑定网卡的时候，出现失败，可使用`sudo dmesg | tail`协助判断错误。

# 安装

```
sudo apt install meson
sudo apt install python3-pip
sudo python3 -m pip install pyelftools
```

```
cd ~
wget https://fast.dpdk.org/rel/dpdk-22.11.1.tar.xz
tar -xf dpdk-22.11.1.tar.xz
cd dpdk-22.11.1
meson -Dexamples=all build
ninja -C build
sudo ninja -C build install
```

# 初始化

## 绑定网卡驱动

如果运行dpdk的网口连接了网线，那么系统的默认驱动会激活该网口，从而导致无法将网卡绑定到vfio-pci驱动，因此需要首先执行命令：

```
sudo ifconfig enp1s0f0 down
```

然后再绑定驱动：

```
sudo ./usertools/dpdk-devbind.py --bind=vfio-pci "01:00.*"
```

## 设置hugepages

```
./setup.hugepages.sh
```

# 检查安装是否正常

## Hello World

```
cd ~/dpdk-22.11.1
sudo ./build/examples/dpdk-helloworld
```

我的电脑上有1张网卡，上面有2个网口，CPU有4个核，所以输出是这样的：

```
EAL: Detected CPU lcores: 4
EAL: Detected NUMA nodes: 1
EAL: Detected static linkage of DPDK
EAL: Multi-process socket /var/run/dpdk/rte/mp_socket
EAL: Selected IOVA mode 'VA'
EAL: VFIO support initialized
EAL: Using IOMMU type 1 (Type 1)
EAL: Ignore mapping IO port bar(2)
EAL: Probe PCI driver: net_e1000_igb (8086:10c9) device: 0000:01:00.0 (socket -1)
EAL: Ignore mapping IO port bar(2)
EAL: Probe PCI driver: net_e1000_igb (8086:10c9) device: 0000:01:00.1 (socket -1)
TELEMETRY: No legacy callbacks, legacy socket not created
hello from core 1
hello from core 2
hello from core 3
hello from core 0
```

## 转发

dpdk的example中有一个非常简单的转发程序，能够将一个网口的数据原封不懂的转发给另一个网口。

我的电脑的拓扑是这样的：主板上有一个网口eno1，有一块单独的网卡通过PCIE连接到主板上，上面有两个网口dpdk0和dpdk1。

默认情况下，eno1和路由器之间用一条网线连接，作为上网的线路。为了测试，将dpdk0和路由器连接，然后再用网线将dpdk1和eno1连接。

然后运行以下命令：

```
cd ~/dpdk-22.11.1
sudo ./build/examples/dpdk-skeleton
```

运行上述命令之后，dpdk0和dpdk1将会被dpdk-skeleton程序接管，dpdk0收到的任何数据都会直接从dpdk1网口出去，dpdk1收到的数据会从dpdk0网口出去。

所以如果运行正常的话，电脑的表现应该和直接用网线连接eno1和路由器一致，即eno1能够正常获取IP地址，电脑能够正常上网。

使用iperf进行网速测试，在正常情况下(eno1和路由器直连)和转发模式下(eno1 <-> dpdk0 <-> dpdk1 <-> 路由器)都能够跑慢1千兆带宽。
