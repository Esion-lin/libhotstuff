#!/bin/bash
rep=({0..3})
if [[ $# -gt 0 ]]; then
    rep=($@)
fi
for i in "${rep[@]}"; do
    echo "starting replica $i"
    ./examples/hotstuff-app --conf ./hotstuff-sec${i}.conf > ./logs/log${i} 2>&1 &
done
wait
