# 先运行 sudo ./build/bin/nvmf_tgt -m 0x1 -r /var/tmp/nvmf.sock，启动target端
sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_create_transport -t tcp

# =========================================== 存储端设备1的建立与多路径连接 ===========================================
# 创建 NVMe 块设备
sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock bdev_nvme_attach_controller -b NVMe0 -t PCIe -a 0000:03:00.0
# 创建 NVMe-oF Subsystem 1 与监听端口
sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_create_subsystem -a nqn.2016-06.io.spdk:cnode1

sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_subsystem_add_listener nqn.2016-06.io.spdk:cnode1 -t tcp -a 127.0.0.1 -f ipv4 -s 4420

sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_subsystem_add_listener nqn.2016-06.io.spdk:cnode1 -t tcp -a 127.0.0.1 -f ipv4 -s 4421

sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_subsystem_add_listener nqn.2016-06.io.spdk:cnode1 -t tcp -a 127.0.0.1 -f ipv4 -s 4422

sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_subsystem_add_listener nqn.2016-06.io.spdk:cnode1 -t tcp -a 127.0.0.1 -f ipv4 -s 4423

sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_subsystem_add_ns nqn.2016-06.io.spdk:cnode1 NVMe0n1

# 启动 vhost 之后 连接存储端
sudo ./scripts/rpc.py -s /var/tmp/vhost.sock bdev_nvme_attach_controller -b ump1_0 -t tcp -a 127.0.0.1 -f ipv4 -s 4420 -n "nqn.2016-06.io.spdk:cnode1"

sudo ./scripts/rpc.py -s /var/tmp/vhost.sock bdev_nvme_attach_controller -b ump1_1 -t tcp -a 127.0.0.1 -f ipv4 -s 4421 -n "nqn.2016-06.io.spdk:cnode1"

sudo ./scripts/rpc.py -s /var/tmp/vhost.sock bdev_nvme_attach_controller -b ump1_2 -t tcp -a 127.0.0.1 -f ipv4 -s 4422 -n "nqn.2016-06.io.spdk:cnode1"

sudo ./scripts/rpc.py -s /var/tmp/vhost.sock bdev_nvme_attach_controller -b ump1_3 -t tcp -a 127.0.0.1 -f ipv4 -s 4423 -n "nqn.2016-06.io.spdk:cnode1"


# =========================================== 存储端设备2的建立与多路径连接 ===========================================
sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock bdev_malloc_create 1024 512 -b Malloc0

# 创建 NVMe-oF Subsystem 2 与监听端口，并连接块设备
sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_create_subsystem -a nqn.2016-06.io.spdk:cnode2

sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_subsystem_add_listener nqn.2016-06.io.spdk:cnode2 -t tcp -a 127.0.0.1 -f ipv4 -s 4420

sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_subsystem_add_listener nqn.2016-06.io.spdk:cnode2 -t tcp -a 127.0.0.1 -f ipv4 -s 4421

sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_subsystem_add_listener nqn.2016-06.io.spdk:cnode2 -t tcp -a 127.0.0.1 -f ipv4 -s 4422

sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_subsystem_add_listener nqn.2016-06.io.spdk:cnode2 -t tcp -a 127.0.0.1 -f ipv4 -s 4423

sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_subsystem_add_ns nqn.2016-06.io.spdk:cnode2 Malloc0
# 启动 vhost 之后 连接存储端
sudo ./scripts/rpc.py -s /var/tmp/vhost.sock bdev_nvme_attach_controller -b ump2_0 -t tcp -a 127.0.0.1 -f ipv4 -s 4420 -n "nqn.2016-06.io.spdk:cnode2"

sudo ./scripts/rpc.py -s /var/tmp/vhost.sock bdev_nvme_attach_controller -b ump2_1 -t tcp -a 127.0.0.1 -f ipv4 -s 4421 -n "nqn.2016-06.io.spdk:cnode2"

sudo ./scripts/rpc.py -s /var/tmp/vhost.sock bdev_nvme_attach_controller -b ump2_2 -t tcp -a 127.0.0.1 -f ipv4 -s 4422 -n "nqn.2016-06.io.spdk:cnode2"

sudo ./scripts/rpc.py -s /var/tmp/vhost.sock bdev_nvme_attach_controller -b ump2_3 -t tcp -a 127.0.0.1 -f ipv4 -s 4423 -n "nqn.2016-06.io.spdk:cnode2"




# sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock bdev_malloc_create 64 512 -b Malloc0

# sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_subsystem_add_ns nqn.2016-06.io.spdk:cnode1 Malloc0





# IO 下发端
sudo modprobe nbd
# sudo ./scripts/rpc.py -s /var/tmp/vhost.sock nbd_start_disk ump-bdev-bd557ceb-78dd-4854-9169-a3d45d92bfb0 /dev/nbd0
# sudo ../fio/fio -ioengine=libaio -bs=4k -direct=1 -thread -rw=randrw -filename=/dev/nbd0 -name="BS 4KB randrw test" -iodepth=16 -runtime=10
# sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_subsystem_remove_listener nqn.2016-06.io.spdk:cnode1 -t tcp -a 127.0.0.1 -f ipv4 -s 4420 

# #  启动 vhost 之后 连接存储端
# sudo ./scripts/rpc.py -s /var/tmp/vhost.sock bdev_nvme_attach_controller -b Nvme0 -t tcp -a 127.0.0.1 -f ipv4 -s 4420 -n "nqn.2016-06.io.spdk:cnode1"

# sudo ./scripts/rpc.py -s /var/tmp/vhost.sock bdev_nvme_attach_controller -b Nvme1 -t tcp -a 127.0.0.1 -f ipv4 -s 4421 -n "nqn.2016-06.io.spdk:cnode1"
# # IO 下发端
# sudo modprobe nbd
# sudo ./scripts/rpc.py nbd_start_disk ump-bdev-00000000-0000-0000-0000-000000000000 /dev/nbd0
# # sudo fio -ioengine=libaio -bs=4k -direct=1 -thread -rw=randrw -filename=/dev/nbd0 -name="BS 4KB randrw test" -iodepth=16 -runtime=10
# # 移除监听端口再连回去
# sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_subsystem_remove_listener nqn.2016-06.io.spdk:cnode1 -t tcp -a 127.0.0.1 -f ipv4 -s 4421
# sudo ./scripts/rpc.py -s /var/tmp/nvmf.sock nvmf_subsystem_add_listener nqn.2016-06.io.spdk:cnode1 -t tcp -a 127.0.0.1 -f ipv4 -s 4421
