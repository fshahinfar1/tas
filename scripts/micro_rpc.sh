#!/bin/bash

# Usage:
# 
# You may set following environment variables to configure experiments
#   TAS_LIB: path to libtas_interpose.so
#   msg_sz: size of message. It should be the same among client & server config
#   max_pendig:
#   total_conn:

benchdir=$HOME/dev/tas-b/benchmarks/micro_rpc/

# set LD_PRELOAD
if [ -n "$TAS_LIB" ]; then
	ldload="LD_PRELOAD=$TAS_LIB"
else
	echo "warning: TAS_LIB not set"
	ldload="LD_PRELOAD=$HOME/dev/tas/lib/libtas_interpose.so"
fi

if [ "$NO_TAS" = "yes" ]; then
	ldload=""
fi

mode=server
port=1234
if [ -z "$msg_sz" ]; then
	echo "msg_sz not specified: server and client should use the same msg_sz (default 8192)"
	msg_sz=8192
fi
if [ -z "$max_pending" ]; then
	echo "max_pending not specified"
	max_pending=512
fi
if [ -z "$total_conn" ]; then
	echo "total_conn not specified"
	total_conn=1
fi

if [ $# -ge 1 ]; then
	mode=client
	server_ip=$1
fi

launch_client() {
	cores=1
	f="sudo $ldload "$benchdir/testclient_linux" \
		$server_ip $port $cores foo \
		$msg_sz $max_pending $total_conn"
	echo "$f"
	$f
}

case $mode in
	"server")
		echo Launching a server
		cores=1
		max_flows=8192
		max_bytes=$msg_sz
		sudo $ldload "$benchdir/echoserver_linux" \
			$port $cores foo $max_flows $max_bytes
		;;
	"client")
		echo Launching client
		launch_client
		;;
	*)
		echo unknown
		;;
esac
