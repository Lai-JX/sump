# all: sump.c
# 	gcc -g -O0  -fPIC -shared -o /usr/lib64/ump.so $< -ldl	# -ldl表示动态链接库为系统库

# run:
# 	@LD_PRELOAD=/usr/lib64/ump.so ../../../build/bin/vhost -m 0x2 -r /var/tmp/vhost.sock -s 128 -R --num-trace-entries 0 
# 	# @LD_PRELOAD=/usr/lib64/ump.so ../../../build/bin/iscsi_tgt
all: sump.c sump_ctrl.c sump_data.c sump_util.c
	gcc -g -O0  -fPIC -shared -o /usr/lib64/ump.so $^ -ldl 

debug: sump.c sump_ctrl.c sump_data.c sump_util.c
	gcc -g -O0  -fPIC -shared -o /usr/lib64/ump.so $^ -ldl -DDEBUG_OUT

run:
	@LD_PRELOAD=/usr/lib64/ump.so ../../../build/bin/vhost -m 0x14 -r /var/tmp/vhost.sock -s 128 -R --num-trace-entries 0

push:
	git add .; git commit -m update; git push; git push gitee
