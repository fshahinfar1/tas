#!/bin/bash

if [ -z "$NET_PCI" ]; then
	echo "NET_PCI is not defined"
	exit 1
fi

tas_dir=$HOME/dev/tas
output_dir=$HOME/socket_results/
server=10.0.0.6
server_exp_ip=10.0.0.7
local_ip=10.0.0.5
exp_duration=25
ssh_user=hawk

# values: tas, socket
mode=socket

clean_up_everthing() {
	echo 'cleaning every thing'
	ssh $ssh_user@$server <<EOF
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

setup_server_with_tas() {
	sz=$1
	echo 'about to setup server'
	ssh $ssh_user@$server << EOF
	cd $tas_dir/scripts
	(NET_PCI="\$NET_PCI" IP="$server_exp_ip" nohup bash ./tas_up.sh &> /tmp/tas) &
	sleep 5
	(TAS_LIB="\$TAS_LIB" \
		msg_sz=$sz max_pending=1 total_conn=512 \
		nohup bash ./micro_rpc.sh &> /tmp/server) &
	sleep 1
EOF
}

setup_server_with_socket() {
	sz=$1
	echo 'about to setup server'
	ssh $ssh_user@$server << EOF
	cd $tas_dir/scripts
	(NO_TAS=yes TAS_LIB="\$TAS_LIB" \
		msg_sz=$sz max_pending=1 total_conn=512 \
		nohup bash ./micro_rpc.sh &> /tmp/server) &
	sleep 1
EOF
}

setup_server() {
	case $mode in
		tas)
			setup_server_with_tas $@
			;;
		socket)
			setup_server_with_socket $@
			;;
		*)
			echo "unexpected mode"
			exit 1
			;;
	esac
}

run_exp() {
	# bring up tas engine
	sz=$1
	wnd=$2
	_tmp_no_tas=yes
	cd $tas_dir/scripts/
	if [ $mode = "tas" ]; then
		_tmp_no_tas=no
		(NET_PCI=$NET_PCI IP=$local_ip nohup bash ./tas_up.sh &> /tmp/tas < /dev/null) &
		sleep 5
	fi
	# run the client
	output_file="$output_dir"/"msg_sz_${sz}_wnd_sz_${wnd}.txt"
	(NO_TAS=$_tmp_no_tas msg_sz=$sz max_pending=1 total_conn=$wnd \
		./micro_rpc.sh "$server_exp_ip" < /dev/null | tee "$output_file") &
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

one_round() {
	sz=$1
	wnd=$2
	clean_up_everthing &> /dev/null
	sleep 1
	setup_server $sz &> /dev/null
	run_exp $sz $wnd
	sleep 1
	clean_up_everthing
	sleep 1
}

main() {
	if [ ! -d $output_dir ]; then
		mkdir -p $output_dir
	fi

	echo "Running fixed window size experiments:"
	echo "======================================================="
	list=( 32 64 128 256 512 1024 2048 4096 8192 )
	wnd=512 # fixed window
	for sz in "${list[@]}"; do
		if [ $running -eq 0 ]; then
			break
		fi
		echo "message size: $sz     window size: $wnd"
		one_round $sz $wnd
		echo "......................................................."
	done

	echo "Running fixed message size experiments:"
	echo "======================================================="
	list=( 1 2 4 8 16 32 64 128 256 512 )
	sz=8192 # fixed message size
	for wnd in "${list[@]}"; do
		if [ $running -eq 0 ]; then
			break
		fi
		echo "message size: $sz     window size: $wnd"
		one_round $sz $wnd
		echo "......................................................."
	done
}

# test
# clean_up_everthing &> /dev/null
# setup_server 8192

main

echo Done!
