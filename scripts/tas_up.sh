#!/bin/bash
set -x

if [ -z "$NET_PCI" ]; then
	echo '$NET_PCI not defined'
	exit -1
fi

CURDIR=$(realpath $(dirname $0))
TAS_DIR=$(realpath $CURDIR/../)
TAS_BIN=$TAS_DIR/tas/tas

IP=$( ip addr | grep 192.168. | cut -d ' ' -f 6 | cut -d '/' -f 1 )

sudo $TAS_BIN --ip-addr=$IP --fp-cores-max=2 \
	--dpdk-extra='-l' --dpdk-extra="2,4,6,8" \
	--dpdk-extra='-a' --dpdk-extra=$NET_PCI \
#

