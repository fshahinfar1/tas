/*
 * Copyright 2019 University of Washington, Max Planck Institute for
 * Software Systems, and The University of Texas at Austin
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <rte_version.h>

#if RTE_VER_YEAR < 20
// KNI has been deprecated
#include <rte_kni.h>
#endif

#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_ethdev.h>

#include <tas.h>
#include "internal.h"

#define MBUF_SIZE (1500  + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define POOL_SIZE (4 * 4096)
#define KNI_MTU 1500

enum change_linkstate {
  LST_NOOP = 0,
  LST_UP,
  LST_DOWN,
};

static int interface_set_carrier(const char *name, int status);
static int op_config_network_if(uint16_t port_id, uint8_t if_up);
#if RTE_VER_YEAR >= 18
static int op_config_mac_address(uint16_t port_id, uint8_t mac_addr[]);
#endif

static struct rte_mempool *kni_pool;

#if RTE_VER_YEAR < 20
static struct rte_kni *kni_if;
static struct rte_kni_conf conf;
static struct rte_kni_ops ops = {
    .port_id = 0,
    .config_network_if = op_config_network_if,
#if RTE_VER_YEAR >= 18
    .config_mac_address = op_config_mac_address,
#endif
  };
#else // RTE_VER_YEAR >= 20
// use this portid for sending/receiving on virtio-user port
static int virt_port = -1;
#endif

static int change_linkstate = LST_NOOP;

#define RX_RING_SIZE 2048

#if RTE_VER_YEAR >= 20
static int __initialize_port(int port)
{
  int rx_rings = 1;
  int tx_rings = 1;
  uint16_t nb_rxd = RX_RING_SIZE;
  uint16_t nb_txd = 4096;
  struct rte_eth_conf port_conf = {};

  char pool_name[64];
  sprintf(pool_name, "virtio_rx_%d", port);
  struct rte_mempool *mbuf_pool = rte_mempool_create(pool_name,
      POOL_SIZE, MBUF_SIZE, 32, sizeof(struct rte_pktmbuf_pool_private),
      rte_pktmbuf_pool_init, NULL, rte_pktmbuf_init, NULL, rte_socket_id(), 0);
  if (mbuf_pool == NULL) {
    fprintf(stderr, "init port rte_mempool_create failed\n");
    return -1;
  }

  if (!(rte_eth_dev_is_valid_port(port))) {
    printf("port is not valid\n");
    return -1;
  }

  int retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
  if (retval != 0) {
    printf("failed to configure the port\n");
    return -1;
  }

  retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
  if (retval != 0) {
    printf("failed to adjust the number of descriptors\n");
    return -1;
  }

  // rx allocate queues
  for (int q = 0; q < rx_rings; q++) {
    retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
        rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    if (retval != 0) {
      printf("failed to setup the queue\n");
      return -1;
    }
  }

  // tx allocate queues
  struct rte_eth_dev_info dev_info;
  struct rte_eth_txconf *txconf;
  rte_eth_dev_info_get(0, &dev_info);
  txconf = &dev_info.default_txconf;

  for (int q = 0; q < tx_rings; q++) {
    retval = rte_eth_tx_queue_setup(port, q, nb_txd,
        rte_eth_dev_socket_id(port), txconf);
    if (retval != 0)
      return -1;
  }

  // start ethernet port
  retval = rte_eth_dev_start(port);
  if (retval < 0) {
    printf("fialed to start the port\n");
    return retval;
  }
  return 0;
}

static int init_virtio_user(void)
{
  printf("initializing virtio_user\n");
  int portid;
  int nb_ports = 1;
  virt_port = rte_eth_dev_count_avail();

  /* Create a vhost_user port for each physical port */
  unsigned port_count = 0;
  RTE_ETH_FOREACH_DEV(portid) {
    char portname[32];
    char portargs[256];
    struct rte_ether_addr addr = {0};

    /* once we have created a virtio port for each physical port, stop creating more */
    if (++port_count > nb_ports)
      break;

    /* get MAC address of physical port to use as MAC of virtio_user port */
    rte_eth_macaddr_get(portid, &addr);

    /* set the name and arguments */
    snprintf(portname, sizeof(portname), "virtio_user%u", portid);
    snprintf(portargs, sizeof(portargs),
        "path=/dev/vhost-net,queues=1,queue_size=%u,iface=%s,mac=" RTE_ETHER_ADDR_PRT_FMT,
        RX_RING_SIZE, portname, RTE_ETHER_ADDR_BYTES(&addr));

    /* add the vdev for virtio_user */
    if (rte_eal_hotplug_add("vdev", portname, portargs) < 0)
      rte_exit(EXIT_FAILURE, "Cannot create paired port for port %u\n", portid);

  }
  __initialize_port(virt_port);
  return 0;
}
#endif

int kni_init(void)
{
  if (config.kni_name == NULL)
    return 0;

#if RTE_VER_YEAR >= 20
  // KNI has been deprecated and not avialable anymore
  if (init_virtio_user() != 0) {
    fprintf(stderr, "failed to prepare the virtio user\n");
    return -1;
  }
#else

#if RTE_VER_YEAR < 19
  rte_kni_init(1);
#else
  if (rte_kni_init(1) != 0) {
    fprintf(stderr, "kni_init: rte_kni_init failed\n");
    return -1;
  }
#endif

#endif

  /* alloc mempool for kni */
  kni_pool = rte_mempool_create("tas_kni", POOL_SIZE, MBUF_SIZE, 32,
      sizeof(struct rte_pktmbuf_pool_private), rte_pktmbuf_pool_init, NULL,
      rte_pktmbuf_init, NULL, rte_socket_id(), 0);
  if (kni_pool == NULL) {
    fprintf(stderr, "kni_init: rte_mempool_create failed\n");
    return -1;
  }

#if RTE_VER_YEAR < 20
  /* initialize config */
  memset(&conf, 0, sizeof(conf));
  strncpy(conf.name, config.kni_name, RTE_KNI_NAMESIZE - 1);
  conf.name[RTE_KNI_NAMESIZE - 1] = 0;
  conf.mbuf_size = MBUF_SIZE;
#if RTE_VER_YEAR >= 18
  memcpy(conf.mac_addr, &eth_addr, sizeof(eth_addr));
  conf.mtu = KNI_MTU;
#endif

  /* allocate kni */
  if ((kni_if = rte_kni_alloc(kni_pool, &conf, &ops)) == NULL) {
    fprintf(stderr, "kni_init: rte_kni_alloc failed\n");
    return -1;
  }
#endif

  return 0;
}

void kni_packet(const void *pkt, uint16_t len)
{
  struct rte_mbuf *mb;
  void *dst;

  if (config.kni_name == NULL)
    return;

  if ((mb =  rte_pktmbuf_alloc(kni_pool)) == NULL) {
    fprintf(stderr, "kni_packet: mbuf alloc failed\n");
    return;
  }

  if ((dst = rte_pktmbuf_append(mb, len)) == NULL) {
    fprintf(stderr, "kni_packet: mbuf append failed\n");
    return;
  }

  memcpy(dst, pkt, len);
#if RTE_VER_YEAR < 20
  if (rte_kni_tx_burst(kni_if, &mb, 1) != 1) {
    fprintf(stderr, "kni_packet: send failed\n");
    rte_pktmbuf_free(mb);
  }
#else
  if (rte_eth_tx_burst(virt_port, 0/*queue id*/, &mb, 1) != 1) {
    fprintf(stderr, "kni_packet: send failed\n");
    rte_pktmbuf_free(mb);
  }
#endif

}

unsigned kni_poll(void)
{
  unsigned n;
  struct rte_mbuf *mb;
  uint32_t op;
  void *buf;

  if (config.kni_name == NULL)
    return 0;

#if RTE_VER_YEAR < 20
  if (change_linkstate != LST_NOOP) {
    if (interface_set_carrier(config.kni_name, change_linkstate == LST_UP)) {
      fprintf(stderr, "kni_poll: linkstate update failed\n");
    }
    change_linkstate = LST_NOOP;
  }

  rte_kni_handle_request(kni_if);

  n = rte_kni_rx_burst(kni_if, &mb, 1);
  if (n == 1) {
    if (nicif_tx_alloc(rte_pktmbuf_pkt_len(mb), &buf, &op) == 0) {
      memcpy(buf, rte_pktmbuf_mtod(mb, void *), rte_pktmbuf_pkt_len(mb));
      nicif_tx_send(op, 1);
    } else {
      fprintf(stderr, "kni_poll: send failed\n");
    }

    rte_pktmbuf_free(mb);
  }
  return 1;
#else
  n = rte_eth_rx_burst(virt_port, 0, &mb, 1);
  if (n == 1) {
    if (nicif_tx_alloc(rte_pktmbuf_pkt_len(mb), &buf, &op) == 0) {
      memcpy(buf, rte_pktmbuf_mtod(mb, void *), rte_pktmbuf_pkt_len(mb));
      nicif_tx_send(op, 1);
    } else {
      fprintf(stderr, "kni_poll: virtio: send failed\n");
    }

    rte_pktmbuf_free(mb);
  }
  return 1;
#endif
}

static int interface_set_carrier(const char *name, int status)
{
  char path[64];
  int fd, ret;
  const char *st = status ? "1" : "0";

  sprintf(path, "/sys/class/net/%s/carrier", name);

  if ((fd = open(path, O_WRONLY)) < 0) {
    perror("interface_set_carrier: open failed");
    return -1;
  }

  if ((ret = write(fd, st, 2)) != 2) {
    perror("interface_set_carrier: write failed");
    close(fd);
    return -1;
  }

  close(fd);
  return 0;
}

static int op_config_network_if(uint16_t port_id, uint8_t if_up)
{
  change_linkstate = (if_up ? LST_UP : LST_DOWN);
  return 0;
}

#if RTE_VER_YEAR >= 18
static int op_config_mac_address(uint16_t port_id, uint8_t mac_addr[])
{
  return 0;
}
#endif
