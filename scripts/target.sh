#! /bin/bash

# 0. 路径设置等
cd /home/ljx/share/spdk/module/sump/scripts
. kill.sh
kill_all "nvmf_tgt"
cd ../../../

# 1. 启动存储端
./build/bin/nvmf_tgt -m 0x1 -r /var/tmp/nvmf.sock &
sleep 1

./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_create_transport -t tcp

# 创建 NVMe-oF Subsystem 与监听端口
./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_create_subsystem -a nqn.2016-06.io.spdk:cnode1

./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_subsystem_add_listener nqn.2016-06.io.spdk:cnode1 -t tcp -a 10.250.54.109 -f ipv4 -s 4420

./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_subsystem_add_listener nqn.2016-06.io.spdk:cnode1 -t tcp -a 10.250.54.109 -f ipv4 -s 4421

./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_subsystem_add_listener nqn.2016-06.io.spdk:cnode1 -t tcp -a 10.250.54.109 -f ipv4 -s 4422

./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_subsystem_add_listener nqn.2016-06.io.spdk:cnode1 -t tcp -a 10.250.54.109 -f ipv4 -s 4423


# 创建 NVMe 块设备并加入 NVMe-oF Subsystem
#  ./scripts/rpc.py -s /var/tmp/nvmf.sock bdev_nvme_attach_controller -b NVMe0 -t PCIe -a 0000:03:00.0

#  ./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_subsystem_add_ns nqn.2016-06.io.spdk:cnode1 NVMe0n1

./scripts/rpc.py -s /var/tmp/nvmf.sock bdev_malloc_create 64 512 -b Malloc0

./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_subsystem_add_ns nqn.2016-06.io.spdk:cnode1 Malloc0