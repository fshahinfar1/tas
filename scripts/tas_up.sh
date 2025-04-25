#!/bin/bash
set -x

if [ -z "$NET_PCI" ]; then
	echo '$NET_PCI not defined'
	exit -1
fi

CURDIR=$(realpath $(dirname $0))
TAS_DIR=$(realpath $CURDIR/../)
TAS_BIN=$TAS_DIR/tas/tas

# MODE=baremetal
MODE=vm

if [ -z $IP ]; then
	echo "IP is not set, trying to guess (searching for 192.168.0.0/16)"
	IP=$( ip addr | grep 192.168. | cut -d ' ' -f 6 | cut -d '/' -f 1 )
fi

case $MODE in
	baremetal)
		sudo $TAS_BIN --ip-addr=$IP --fp-cores-max=2 \
			--dpdk-extra='-l' --dpdk-extra="2,3,4,5" \
			--dpdk-extra='-a' --dpdk-extra=$NET_PCI
		;;
	vm)
		echo running insde a vm
		sudo $TAS_BIN --ip-addr=$IP --fp-cores-max=2 \
			--fp-no-ints --fp-no-autoscale \
			--dpdk-extra='-l' --dpdk-extra="2,3,4,5" \
			--dpdk-extra='-a' --dpdk-extra=$NET_PCI
		;;
	*)
		echo Unexpected mode
		exit 1
		;;
esac

#

