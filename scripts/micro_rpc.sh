#!/bin/bash

benchdir=$HOME/dev/tas-b/benchmarks/micro_rpc/

# set LD_PRELOAD
if [ -n $TAS_LIB ]; then
	ldload="LD_PRELOAD=$TAS_LIB"
else
	ldload="LD_PRELOAD=$HOME/dev/tas/libs/libtas_interpose.so"
fi

mode=server
port=1234
if [ $# -ge 1 ]; then
	mode=client
	server_ip=$1
fi

msg_sz=8192
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
		cores=1
		max_pending=512
		total_conn=1
		sudo $ldload "$benchdir/testclient_linux" \
			$server_ip $port $cores foo \
			$msg_sz $max_pending $total_conn
		;;
	*)
		echo unknown
		;;
esac
