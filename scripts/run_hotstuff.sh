#!/bin/bash
rep=({0..3})
if [[ $# -gt 0 ]]; then
    rep=($@)
fi
reppid0=0
reppid1=0
reppid2=0
reppid3=0
for i in "${rep[@]}"; do
    echo "starting replica $i"
    ./examples/hotstuff-app --conf ./hotstuff-sec${i}.conf > ./logs/log${i} 2>&1 &
    eval reppid${i}=$!
    eval echo \$reppid${i}
done
count=0
while true
do
   count=$(( (${count} + 1) % 4 ))
   eval nowpid=\$reppid${count}
   #echo $nowpid
   if ps -p $nowpid > /dev/null 2>&1
   then
	sleep 10
   else	
	./examples/hotstuff-app --conf ./hotstuff-sec${count}.conf > ./logs/log${count} 2>&1 &
	eval reppid${cout}=$!
	echo "restart replica ${count}"
	sleep 10
   fi
done
wait
