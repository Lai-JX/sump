#! /bin/bash

# 在sump/scripts目录下运行
cd /home/ljx/share/spdk/module/sump/scripts
. kill.sh
kill_all "vhost"

method=robin                                    # -m 负载均衡算法
addr=10.251.176.136                             # -a 地址,，如10.251.176.136
log_path=""                                     # -l 日志输出路径
fio=""                                          # -q nbd or qemu
subsystem_name="nqn.2016-06.io.spdk:cnode1"     # -n 子系统名字

# 获取参数
while getopts "m:a:q:l:h" arg #选项后面的冒号表示该选项需要参数
do
        case $arg in
             a)
                addr=$OPTARG
                ;;
             h)
                echo "-a                The address of the storage target."
                echo "-h                Print this help message."
                echo "-l                The path of log file.(include file name)"
                echo "-m                The algorithm of load balancing.(TIME,ROBIN,QUEUE,RANDOM,WEIGHT,HASH)" 
                echo "-n                The name of subsystem." 
                echo "-q                I/O method. (nbd or qemu)"
                ;;
             l)
                log_path=$OPTARG
                ;;
             m)
                method=$OPTARG
                ;;
             n)
                subsystem_name=$OPTARG
                ;;
             q)
                fio=$OPTARG
                ;;
             ?)  #当有不认识的选项的时候arg为?
            echo "unkonw argument"
        exit 1
        ;;
        esac
done

# 编译
cd ../

cd ./nvme
sudo make
sudo make install
cd ../

cd ./nvmf
sudo make
sudo make install
cd ../

sudo make ${method}

# 运行主机端，启动vhost
if [[ ${log_path} = "" ]]
then
    echo "log path is null"
    sudo make run &
else
    sudo make run > ${log_path} &
fi

sleep 1

cd ../../

# 启动 vhost 之后 连接存储端 
sudo ./scripts/rpc.py -s /var/tmp/vhost.sock bdev_nvme_attach_controller -b ump1_0 -t tcp -a ${addr} -f ipv4 -s 4420 -n ${subsystem_name}

sudo ./scripts/rpc.py -s /var/tmp/vhost.sock bdev_nvme_attach_controller -b ump1_1 -t tcp -a ${addr} -f ipv4 -s 4421 -n ${subsystem_name}

sudo ./scripts/rpc.py -s /var/tmp/vhost.sock bdev_nvme_attach_controller -b ump1_2 -t tcp -a ${addr} -f ipv4 -s 4422 -n ${subsystem_name}

sudo ./scripts/rpc.py -s /var/tmp/vhost.sock bdev_nvme_attach_controller -b ump1_3 -t tcp -a ${addr} -f ipv4 -s 4423 -n ${subsystem_name}

# 挂载
if [[ ${fio} = "qemu" ]]
then
    echo "qemu"
else                        # 默认为nbd
    sudo modprobe nbd
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
fi



