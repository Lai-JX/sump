# 先运行 sudo ./build/bin/nvmf_tgt -m 0x1 -r /var/tmp/nvmf.sock，启动target端
sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_create_transport -t tcp

# 创建 NVMe-oF Subsystem 与监听端口
sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_create_subsystem -a nqn.2016-06.io.spdk:cnode1

sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_subsystem_add_listener nqn.2016-06.io.spdk:cnode1 -t tcp -a 127.0.0.1 -f ipv4 -s 4420

sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_subsystem_add_listener nqn.2016-06.io.spdk:cnode1 -t tcp -a 127.0.0.1 -f ipv4 -s 4421

# 创建 NVMe 块设备并加入 NVMe-oF Subsystem
# sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock bdev_nvme_attach_controller -b NVMe0 -t PCIe -a 0000:03:00.0

# sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_subsystem_add_ns nqn.2016-06.io.spdk:cnode1 NVMe0n1

sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock bdev_malloc_create 64 512 -b Malloc0

sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_subsystem_add_ns nqn.2016-06.io.spdk:cnode1 Malloc0