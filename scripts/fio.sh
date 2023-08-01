# 设备1
sudo fio -ioengine=libaio -bs=4k -direct=1 -thread -rw=randrw -size=2048M -numjobs=2 -runtime=60 -time_based=1 -filename=/dev/nbd0 -name="BS 4KB randrw test" -iodepth=16 &

# 设备2
sudo fio -ioengine=libaio -bs=4k -direct=1 -thread -rw=randrw -size=1024M -numjobs=2 -runtime=60 -time_based=1 -filename=/dev/nbd1 -name="BS 4KB randrw test" -iodepth=16 &