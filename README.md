* 简单示范使用虚拟网卡实现vpn的大致思路

# server:
``` shell
# compile
make server
# run vpn server
sudo ./server
# NAT
sudo iptables -t nat -A POSTROUTING -s 10.0.0.0/8 -o <INTERFACE> -j M^CQUERADE
# 将INTERFACE替换为物理网卡名称
```

# client:
``` shell
# compile
make client
# run vpn client
sudo ./client --server-address <ADDRESS>
```

