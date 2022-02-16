#!/bin/bash

start_test() {
echo -e "\t\t\t\t Starting futex Tests" 
cd /mnt/ && make -C tools/testing/selftests TARGETS=futex
cd /mnt/tools/testing/selftests/futex/ && ./run.sh
echo -e "\t\t\t\t Completed futex Tests" 
echo "========================================================================" 
echo -e "\t\t\t\t Starting syscall_user_dispatch:sud_test Tests"
cd /mnt/ && make -C tools/testing/selftests TARGETS=syscall_user_dispatch
cd /mnt/tools/testing/selftests/syscall_user_dispatch && ./sud_test
echo -e "\t\t\t\t Completed syscall_user_dispatch:sud_test Tests"
echo "========================================================================"
echo -e "\t\t\t\t Starting syscall_user_dispatch:sud_benchmark Tests"
./sud_benchmark 
echo -e "\t\t\t\t Completed syscall_user_dispatch:sud_benchmark Tests"
echo "========================================================================" 
echo -e "\t\t\t\t Build Perf benchmark"
echo "========================================================================" 
cd /mnt
make headers_install INSTALL_HDR_PATH=/usr
echo "grep FUTEX_32"
grep FUTEX_32 /usr/include/linux/futex.h
apt-get update && apt-get install -y elfutils libunwind-dev binutils numactl libaudit-dev coreutils libelf-dev libzstd-dev libcap-dev
apt-get update && apt-get install -y flex bison build-essential 
apt-get update && apt-get install -y --fix-missing libiberty-dev libbabeltrace-ctf-dev libperl-dev libslang2-dev libssl-dev systemtap-sdt-dev libdw-dev
cd /mnt/tools/perf/ && make

echo -e "\t\t\t\t Completed perf benchmark build" 
echo "========================================================================" 
echo -e "\t\t\t\t Run Perf benchmark"
echo "========================================================================" 

./perf bench futex2 hash -s
./perf bench futex2 hash -s -S
./perf bench -r 50 futex2 wake -s
./perf bench -r 50 futex2 wake -s -S
./perf bench -r 50 futex2 wake-parallel -s
./perf bench -r 50 futex2 wake-parallel -s -S
./perf bench -r 50 futex2 wake -s -t 1000
./perf bench -r 50 futex2 wake -s -S  -t 1000
./perf bench -r 50 futex2 wake-parallel -s -t 1000
./perf bench -r 50 futex2 wake-parallel -s -S -t 1000

echo -e "\t\t\t\t Completed perf run" 
}

start_test 2>&1 | tee -a /mnt/kernel_results.log

# Check Interception overhead
MAX_OVERHEAD="10.00"
found_text=$(grep "Interception" /mnt/kernel_results.log)
result=$(echo $found_text | grep -Eo '[0-9]+([.][0-9]+)')
min=$(echo $result $MAX_OVERHEAD | awk '{if ($1 < $2) print "ok"; else print "not_ok"}')
if [ $min == "not_ok" ]; then
echo "Interception overhead greater than 10%" > /mnt/fail.txt
fi

# Parse result file for fail value
grep -q "fail:[1-9]" /mnt/kernel_results.log
ret=$?
if [ $ret -ne 0 ];
then
	touch /mnt/pass.txt
	sync
else
	touch /mnt/fail.txt
	sync
fi
poweroff
