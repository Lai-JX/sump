#! /bin/bash

# 先杀死之前存在的相同进程
sudo ./scripts/stop.sh
# 启动nvme（pcie）      
sudo ../../scripts/setup.sh

# 编译nvme模块
cd ./nvme
make
make install
cd ../

# 1. vhost端
make debug
make run &
sleep 1
echo "========================================================================================="

# 2. target端
cd ../../
./build/bin/nvmf_tgt -m 0x1 -r /var/tmp/nvmf.sock &
sleep 1
echo "========================================================================================="

# 3. 连接
./module/sump/scripts/vhost_test.sh > log.txt
echo "========================================================================================="

# 4. 挂载
bdev_output=`./scripts/rpc.py -s /var/tmp/vhost.sock bdev_get_bdevs | grep "name" | awk '{print $2}'`
i=0
echo $bdev_output
for bdev in $bdev_output
do
    if [ `expr $i % 2` == 0 ]
    then
        echo ${bdev:1:-2}
        ./scripts/rpc.py -s /var/tmp/vhost.sock nbd_start_disk ${bdev:1:-2} /dev/nbd`expr $i / 2`
    fi
    i=`expr $i + 1`
done
# ./scripts/rpc.py -s /var/tmp/vhost.sock nbd_start_disk  /dev/nbd0            

# 5. 下发io
# sudo ../fio/fio -ioengine=libaio -bs=4k -direct=1 -thread -rw=randrw -filename=/dev/nbd0 -name="BS 4KB randrw test" -iodepth=16 -runtime=10







