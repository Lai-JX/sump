# 设备1
sudo fio -ioengine=libaio -bs=32M -direct=1 -thread -rw=randrw -size=10G -numjobs=2 -filename=/dev/nbd0 -name="BS 4KB randrw test" -iodepth=16 &
# sudo fio -ioengine=libaio -bs=4k -direct=1 -thread -rw=randrw -size=512M -numjobs=2 -filename=/dev/nbd0 -name="BS 4KB randrw test" -iodepth=16 &
# sudo fio -ioengine=libaio -bs=4k -direct=1 -thread -rw=randread -size=32M -numjobs=2 -filename=/dev/nbd0 -name="BS 4KB randrw test" -iodepth=16 &
# -runtime=120 -time_based=1 

# 设备2
# sudo fio -ioengine=libaio -bs=4k -direct=1 -thread -rw=randrw -size=1024M -numjobs=2 -runtime=120 -time_based=1 -filename=/dev/nbd1 -name="BS 4KB randrw test" -iodepth=16 &
# sudo fio -ioengine=libaio -bs=4k -direct=1 -thread -rw=randrw -size=1024M -numjobs=2 -filename=/dev/nbd1 -name="BS 4KB randrw test" -iodepth=16 &