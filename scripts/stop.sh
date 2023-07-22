# kill_all: 根据命令杀死对应的进程
kill_all()
{
    ID=`ps -ef | grep "$1" | grep -v "$0" | grep -v "grep" | awk '{print $2}'`  # -v表示反过滤，awk表示按空格或tab键拆分，{print $2}表示打印第二个（这里对应进程号）
    for id in $ID 
    do  
    kill -9 $id  
    echo "killed $id"  
    done
}

kill_all "../../build/bin/vhost -m 0x14 -r /var/tmp/vhost.sock -s 128 -R --num-trace-entries 0"
kill_all "./build/bin/nvmf_tgt -m 0x1 -r /var/tmp/nvmf.sock"