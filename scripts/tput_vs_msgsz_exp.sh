#!/bin/bash

if [ -z "$NET_PCI" ]; then
	echo "NET_PCI is not defined"
	exit 1
fi

tas_dir=$HOME/dev/tas
output_dir=$HOME/results/
out_standing_msgs=8
msg_sz=( 32 64 128 256 512 1024 2048 4096 8192 )
server=128.105.146.99
server_exp_ip=192.168.1.1
exp_duration=20

clean_up_everthing() {
	echo 'cleaning every thing'
	ssh farbod@$server <<EOF
	sudo pkill echoserver_linu
	sleep 1
	sudo pkill tas
	sleep 1
	sudo rm /dev/hugepages/tas_*
EOF

	sudo pkill testclient_linu
	sudo pkill tas
	sudo rm /dev/hugepages/tas_*
}

setup_server() {
	sz=$1
	echo 'about to setup server'
	ssh farbod@$server << EOF
	source \$HOME/.bashrc
	cd $tas_dir/scripts
	(NET_PCI=$NET_PCI nohup bash ./tas_up.sh &> /tmp/tas) &
	sleep 5
	(TAS_LIB=$TAS_LIB \
		msg_sz=$sz \
		nohup bash ./micro_rpc.sh &> /tmp/server) &
	sleep 1
EOF
}

run_exp() {
	# bring up tas engine
	sz=$1
	cd $tas_dir/scripts/
	(NET_PCI=$NET_PCI nohup bash ./tas_up.sh &> /tmp/tas) &
	sleep 5
	# run the client
	output_file="$output_dir"/"msg_sz_$sz.txt"
	(msg_sz=$sz max_pending=$out_standing_msgs \
	./micro_rpc.sh "$server_exp_ip" |  tee "$output_file") &
	sleep $exp_duration
}

running=1
stop_exp() {
	echo received signal...
	echo cleaning...
	running=0
	clean_up_everthing &> /dev/null
	exit -1
}

trap "stop_exp" SIGINT SIGHUP

main() {
	if [ ! -d $output_dir ]; then
		mkdir -p $output_dir
	fi

	for sz in "${msg_sz[@]}"; do
		if [ $running -eq 0 ]; then
			break
		fi
		echo "======================================================="
		echo Running measurment with message size = $sz
		clean_up_everthing &> /dev/null
		setup_server $sz &> /dev/null
		run_exp $sz
		sleep 1
		clean_up_everthing
		sleep 1
	done
}

# test
# clean_up_everthing
# setup_server 32

main

echo Done!
