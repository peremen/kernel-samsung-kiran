/*
 * u_ether.c -- Ethernet-over-USB link layer utilities for Gadget stack
 *
 * Copyright (C) 2003-2005,2008 David Brownell
 * Copyright (C) 2003-2004 Robert Schwebel, Benedikt Spranger
 * Copyright (C) 2008 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* #define VERBOSE_DEBUG */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/device.h>
#include <linux/ctype.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>

#include "u_ether.h"


/*
 * This component encapsulates the Ethernet link glue needed to provide
 * one (!) network link through the USB gadget stack, normally "usb0".
 *
 * The control and data models are handled by the function driver which
 * connects to this code; such as CDC Ethernet (ECM or EEM),
 * "CDC Subset", or RNDIS.  That includes all descriptor and endpoint
 * management.
 *
 * Link level addressing is handled by this component using module
 * parameters; if no such parameters are provided, random link level
 * addresses are used.  Each end of the link uses one address.  The
 * host end address is exported in various ways, and is often recorded
 * in configuration databases.
 *
 * The driver which assembles each configuration using such a link is
 * responsible for ensuring that each configuration includes at most one
 * instance of is network link.  (The network layer provides ways for
 * this single "physical" link to be used by multiple virtual links.)
 */

#define UETH__VERSION	"29-May-2008"

struct eth_dev {
	/* lock is held while accessing port_usb
	 */
	spinlock_t		lock;
	struct gether		*port_usb;

	struct net_device	*net;
	struct usb_gadget	*gadget;

	spinlock_t		req_lock;	/* guard {rx,tx}_reqs */
	struct list_head	tx_reqs, rx_reqs;
	atomic_t		tx_qlen;

	struct sk_buff_head	rx_frames;

	unsigned		header_len;
	struct sk_buff		*(*wrap)(struct gether *, struct sk_buff *skb);
	int			(*unwrap)(struct gether *,
						struct sk_buff *skb,
						struct sk_buff_head *list);

	struct work_struct	work;

	unsigned long		todo;
#define	WORK_RX_MEMORY		0

	bool			zlp;
	u8			host_mac[ETH_ALEN];
};

/*-------------------------------------------------------------------------*/

#define RX_EXTRA	20	/* bytes guarding against rx overflows */

#define DEFAULT_QLEN	2	/* double buffering by default */

#ifdef CONFIG_USB_SPRD_DWC
static unsigned qmult = 15;
#else
static unsigned qmult = 5;
#endif
module_param(qmult, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(qmult, "queue length multiplier at high/super speed");

/* for dual-speed hardware, use deeper queues at high/super speed */
static inline int qlen(struct usb_gadget *gadget)
{
	if (gadget_is_dualspeed(gadget) && (gadget->speed == USB_SPEED_HIGH ||
					    gadget->speed == USB_SPEED_SUPER))
		return qmult * DEFAULT_QLEN;
	else
		return DEFAULT_QLEN;
}

/*-------------------------------------------------------------------------*/

/* REVISIT there must be a better way than having two sets
 * of debug calls ...
 */

#undef DBG
#undef VDBG
#undef ERROR
#undef INFO

#define xprintk(d, level, fmt, args...) \
	printk(level "%s: " fmt , (d)->net->name , ## args)

#ifdef DEBUG
#undef DEBUG
#define DBG(dev, fmt, args...) \
	xprintk(dev , KERN_DEBUG , fmt , ## args)
#else
#define DBG(dev, fmt, args...) \
	do { } while (0)
#endif /* DEBUG */

#ifdef VERBOSE_DEBUG
#define VDBG	DBG
#else
#define VDBG(dev, fmt, args...) \
	do { } while (0)
#endif /* DEBUG */

#define ERROR(dev, fmt, args...) \
	xprintk(dev , KERN_ERR , fmt , ## args)
#define INFO(dev, fmt, args...) \
	xprintk(dev , KERN_INFO , fmt , ## args)

/*-------------------------------------------------------------------------*/

/* NETWORK DRIVER HOOKUP (to the layer above this driver) */

static int ueth_change_mtu(struct net_device *net, int new_mtu)
{
	struct eth_dev	*dev = netdev_priv(net);
	unsigned long	flags;
	int		status = 0;

	/* don't change MTU on "live" link (peer won't know) */
	spin_lock_irqsave(&dev->lock, flags);
	if (dev->port_usb)
		status = -EBUSY;
	else if (new_mtu <= ETH_HLEN || new_mtu > ETH_FRAME_LEN)
		status = -ERANGE;
	else
		net->mtu = new_mtu;
	spin_unlock_irqrestore(&dev->lock, flags);

	return status;
}

static void eth_get_drvinfo(struct net_device *net, struct ethtool_drvinfo *p)
{
	struct eth_dev *dev = netdev_priv(net);

	strlcpy(p->driver, "g_ether", sizeof(p->driver));
	strlcpy(p->version, UETH__VERSION, sizeof(p->version));
	strlcpy(p->fw_version, dev->gadget->name, sizeof(p->fw_version));
	strlcpy(p->bus_info, dev_name(&dev->gadget->dev), sizeof(p->bus_info));
}

/* REVISIT can also support:
 *   - WOL (by tracking suspends and issuing remote wakeup)
 *   - msglevel (implies updated messaging)
 *   - ... probably more ethtool ops
 */

static const struct ethtool_ops ops = {
	.get_drvinfo = eth_get_drvinfo,
	.get_link = ethtool_op_get_link,
};

static void defer_kevent(struct eth_dev *dev, int flag)
{
	if (test_and_set_bit(flag, &dev->todo))
		return;
	if (!schedule_work(&dev->work))
		ERROR(dev, "kevent %d may have been dropped\n", flag);
	else
		DBG(dev, "kevent %d scheduled\n", flag);
}

static void rx_complete(struct usb_ep *ep, struct usb_request *req);

static int
rx_submit(struct eth_dev *dev, struct usb_request *req, gfp_t gfp_flags)
{
	struct sk_buff	*skb;
	int		retval = -ENOMEM;
	size_t		size = 0;
	struct usb_ep	*out;
	unsigned long	flags;

	spin_lock_irqsave(&dev->lock, flags);
	if (dev->port_usb)
		out = dev->port_usb->out_ep;
	else
		out = NULL;
	spin_unlock_irqrestore(&dev->lock, flags);

	if (!out)
		return -ENOTCONN;


	/* Padding up to RX_EXTRA handles minor disagreements with host.
	 * Normally we use the USB "terminate on short read" convention;
	 * so allow up to (N*maxpacket), since that memory is normally
	 * already allocated.  Some hardware doesn't deal well with short
	 * reads (e.g. DMA must be N*maxpacket), so for now don't trim a
	 * byte off the end (to force hardware errors on overflow).
	 *
	 * RNDIS uses internal framing, and explicitly allows senders to
	 * pad to end-of-packet.  That's potentially nice for speed, but
	 * means receivers can't recover lost synch on their own (because
	 * new packets don't only start after a short RX).
	 */
	size += sizeof(struct ethhdr) + dev->net->mtu + RX_EXTRA;
	size += dev->port_usb->header_len;
	size += out->maxpacket - 1;
	size -= size % out->maxpacket;

	if (dev->port_usb->is_fixed)
		size = max_t(size_t, size, dev->port_usb->fixed_out_len);
	size = size * RNDIS_MSG_MAX_NUM;

	skb = alloc_skb(size + NET_IP_ALIGN, gfp_flags);
	if (skb == NULL) {
		DBG(dev, "no rx skb\n");
		goto enomem;
	}

	/* Some platforms perform better when IP packets are aligned,
	 * but on at least one, checksumming fails otherwise.  Note:
	 * RNDIS headers involve variable numbers of LE32 values.
	 */
	/*
	 * RX: Do not move data by IP_ALIGN:
	 * if your DMA controller cannot handle it
	 */
	if (!gadget_dma32(dev->gadget))
	skb_reserve(skb, NET_IP_ALIGN);

	req->buf = skb->data;
	req->length = size;
	req->complete = rx_complete;
	req->context = skb;

	retval = usb_ep_queue(out, req, gfp_flags);
	if (retval == -ENOMEM)
enomem:
		defer_kevent(dev, WORK_RX_MEMORY);
	if (retval) {
		DBG(dev, "rx submit --> %d\n", retval);
		if (skb)
			dev_kfree_skb_any(skb);
		spin_lock_irqsave(&dev->req_lock, flags);
		list_add(&req->list, &dev->rx_reqs);
		spin_unlock_irqrestore(&dev->req_lock, flags);
	}
	return retval;
}

static void rx_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct sk_buff	*skb = req->context, *skb2;
	struct eth_dev	*dev = ep->driver_data;
	int		status = req->status;

	switch (status) {

	/* normal completion */
	case 0:
		skb_put(skb, req->actual);
		if (gadget_dma32(dev->gadget) && NET_IP_ALIGN) {
			u8 *data = skb->data;
			size_t len = skb_headlen(skb);
			skb_reserve(skb, NET_IP_ALIGN);
			memmove(skb->data, data, len);
		}
		if (dev->unwrap) {
			unsigned long	flags;

			spin_lock_irqsave(&dev->lock, flags);
			if (dev->port_usb) {
				status = dev->unwrap(dev->port_usb,
							skb,
							&dev->rx_frames);
			} else {
				dev_kfree_skb_any(skb);
				status = -ENOTCONN;
			}
			spin_unlock_irqrestore(&dev->lock, flags);
		} else {
			skb_queue_tail(&dev->rx_frames, skb);
		}
		skb = NULL;

		skb2 = skb_dequeue(&dev->rx_frames);
		while (skb2) {
			if (status < 0
					|| ETH_HLEN > skb2->len
					|| skb2->len > VLAN_ETH_FRAME_LEN) {
				dev->net->stats.rx_errors++;
				dev->net->stats.rx_length_errors++;
				DBG(dev, "rx length %d\n", skb2->len);
				dev_kfree_skb_any(skb2);
				goto next_frame;
			}
			skb2->protocol = eth_type_trans(skb2, dev->net);
			dev->net->stats.rx_packets++;
			dev->net->stats.rx_bytes += skb2->len;

			/* no buffer copies needed, unless hardware can't
			 * use skb buffers.
			 */
			status = netif_rx(skb2);
next_frame:
			skb2 = skb_dequeue(&dev->rx_frames);
		}
		break;

	/* software-driven interface shutdown */
	case -ECONNRESET:		/* unlink */
	case -ESHUTDOWN:		/* disconnect etc */
		VDBG(dev, "rx shutdown, code %d\n", status);
		goto quiesce;

	/* for hardware automagic (such as pxa) */
	case -ECONNABORTED:		/* endpoint reset */
		DBG(dev, "rx %s reset\n", ep->name);
		defer_kevent(dev, WORK_RX_MEMORY);
quiesce:
		dev_kfree_skb_any(skb);
		goto clean;

	/* data overrun */
	case -EOVERFLOW:
		dev->net->stats.rx_over_errors++;
		/* FALLTHROUGH */

	default:
		dev->net->stats.rx_errors++;
		DBG(dev, "rx status %d\n", status);
		break;
	}

	if (skb)
		dev_kfree_skb_any(skb);
	if (!netif_running(dev->net)) {
clean:
		spin_lock(&dev->req_lock);
		list_add(&req->list, &dev->rx_reqs);
		spin_unlock(&dev->req_lock);
		req = NULL;
	}
	if (req)
		rx_submit(dev, req, GFP_ATOMIC);
}

static int prealloc(struct list_head *list, struct usb_ep *ep, unsigned n)
{
	unsigned		i;
	struct usb_request	*req;

	if (!n)
		return -ENOMEM;

	/* queue/recycle up to N requests */
	i = n;
	list_for_each_entry(req, list, list) {
		if (i-- == 0)
			goto extra;
	}
	while (i--) {
		req = usb_ep_alloc_request(ep, GFP_ATOMIC);
		if (!req){
			printk(KERN_ERR "%s@%d: usb_ep_alloc_request fail: %d\n", __func__, __LINE__, i);
			return list_empty(list) ? -ENOMEM : 0;
		}
		list_add(&req->list, list);
	}
	return 0;

extra:
	/* free extras */
	for (;;) {
		struct list_head	*next;

		next = req->list.next;
		list_del(&req->list);
		usb_ep_free_request(ep, req);

		if (next == list)
			break;

		req = container_of(next, struct usb_request, list);
	}
	return 0;
}

static int alloc_requests(struct eth_dev *dev, struct gether *link, unsigned n)
{
	int	status;

	spin_lock(&dev->req_lock);
	status = prealloc(&dev->tx_reqs, link->in_ep, n);
	if (status < 0){
		ERROR(dev, "fail to alloc in_ep\n");
		goto fail;
	}
	status = prealloc(&dev->rx_reqs, link->out_ep, n);
	if (status < 0){
		ERROR(dev, "fail to alloc out_ep\n");
		goto fail;
	}
	goto done;
fail:
	DBG(dev, "can't alloc requests\n");
done:
	spin_unlock(&dev->req_lock);
	return status;
}

static void rx_fill(struct eth_dev *dev, gfp_t gfp_flags)
{
	struct usb_request	*req;
	unsigned long		flags;

	/* fill unused rxq slots with some skb */
	spin_lock_irqsave(&dev->req_lock, flags);
	while (!list_empty(&dev->rx_reqs)) {
		req = container_of(dev->rx_reqs.next,
				struct usb_request, list);
		list_del_init(&req->list);
		spin_unlock_irqrestore(&dev->req_lock, flags);

		if (rx_submit(dev, req, gfp_flags) < 0) {
			defer_kevent(dev, WORK_RX_MEMORY);
			return;
		}

		spin_lock_irqsave(&dev->req_lock, flags);
	}
	spin_unlock_irqrestore(&dev->req_lock, flags);
}

static void eth_work(struct work_struct *work)
{
	struct eth_dev	*dev = container_of(work, struct eth_dev, work);

	if (test_and_clear_bit(WORK_RX_MEMORY, &dev->todo)) {
		if (netif_running(dev->net))
			rx_fill(dev, GFP_KERNEL);
	}

	if (dev->todo)
		DBG(dev, "work done, flags = 0x%lx\n", dev->todo);
}
static inline int is_promisc(u16 cdc_filter)
{
	return cdc_filter & USB_CDC_PACKET_TYPE_PROMISCUOUS;
}

#ifndef CONFIG_USB_SPRD_DWC

static void tx_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct sk_buff	*skb = req->context;
	struct eth_dev	*dev = ep->driver_data;

	switch (req->status) {
	default:
		dev->net->stats.tx_errors++;
		VDBG(dev, "tx err %d\n", req->status);
		/* FALLTHROUGH */
	case -ECONNRESET:		/* unlink */
	case -ESHUTDOWN:		/* disconnect etc */
		break;
	case 0:
		dev->net->stats.tx_bytes += skb->len;
	}
	dev->net->stats.tx_packets++;

	spin_lock(&dev->req_lock);
	list_add(&req->list, &dev->tx_reqs);
	spin_unlock(&dev->req_lock);

	dev_kfree_skb_any(skb);

	atomic_dec(&dev->tx_qlen);
	if (netif_carrier_ok(dev->net))
		netif_wake_queue(dev->net);
}

static netdev_tx_t eth_start_xmit(struct sk_buff *skb,
					struct net_device *net)
{
	struct eth_dev		*dev = netdev_priv(net);
	int			length = skb->len;
	int			retval;
	struct usb_request	*req = NULL;
	unsigned long		flags;
	struct usb_ep		*in;
	u16			cdc_filter;

	spin_lock_irqsave(&dev->lock, flags);
	if (dev->port_usb) {
		in = dev->port_usb->in_ep;
		cdc_filter = dev->port_usb->cdc_filter;
	} else {
		in = NULL;
		cdc_filter = 0;
	}
	spin_unlock_irqrestore(&dev->lock, flags);

	if (!in) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	/* apply outgoing CDC or RNDIS filters */
	if (!is_promisc(cdc_filter)) {
		u8		*dest = skb->data;

		if (is_multicast_ether_addr(dest)) {
			u16	type;

			/* ignores USB_CDC_PACKET_TYPE_MULTICAST and host
			 * SET_ETHERNET_MULTICAST_FILTERS requests
			 */
			if (is_broadcast_ether_addr(dest))
				type = USB_CDC_PACKET_TYPE_BROADCAST;
			else
				type = USB_CDC_PACKET_TYPE_ALL_MULTICAST;
			if (!(cdc_filter & type)) {
				dev_kfree_skb_any(skb);
				return NETDEV_TX_OK;
			}
		}
		/* ignores USB_CDC_PACKET_TYPE_DIRECTED */
	}

	spin_lock_irqsave(&dev->req_lock, flags);
	/*
	 * this freelist can be empty if an interrupt triggered disconnect()
	 * and reconfigured the gadget (shutting down this queue) after the
	 * network stack decided to xmit but before we got the spinlock.
	 */
	if (list_empty(&dev->tx_reqs)) {
		spin_unlock_irqrestore(&dev->req_lock, flags);
		return NETDEV_TX_BUSY;
	}

	req = container_of(dev->tx_reqs.next, struct usb_request, list);
	list_del(&req->list);

	/* temporarily stop TX queue when the freelist empties */
	if (list_empty(&dev->tx_reqs))
		netif_stop_queue(net);
	spin_unlock_irqrestore(&dev->req_lock, flags);

	/* no buffer copies needed, unless the network stack did it
	 * or the hardware can't use skb buffers.
	 * or there's not enough space for extra headers we need
	 */
	if (dev->wrap) {
		unsigned long	flags;

		spin_lock_irqsave(&dev->lock, flags);
		if (dev->port_usb)
			skb = dev->wrap(dev->port_usb, skb);
		spin_unlock_irqrestore(&dev->lock, flags);
		if (!skb)
			goto drop;

		length = skb->len;
	}
	/*
	 * Align data to 32bit if the dma controller requires it
	 */
	if (gadget_dma32(dev->gadget)) {
		unsigned long align = (unsigned long)skb->data & 3;
		if (WARN_ON(skb_headroom(skb) < align)) {
			dev_kfree_skb_any(skb);
			goto drop;
		} else if (align) {
			u8 *data = skb->data;
			size_t len = skb_headlen(skb);
			skb->data -= align;
			memmove(skb->data, data, len);
			skb_set_tail_pointer(skb, len);
		}
	}
	req->buf = skb->data;
	req->context = skb;
	req->complete = tx_complete;

	/* NCM requires no zlp if transfer is dwNtbInMaxSize */
	if (dev->port_usb->is_fixed &&
	    length == dev->port_usb->fixed_in_len &&
	    (length % in->maxpacket) == 0)
		req->zero = 0;
	else
		req->zero = 1;

	/* use zlp framing on tx for strict CDC-Ether conformance,
	 * though any robust network rx path ignores extra padding.
	 * and some hardware doesn't like to write zlps.
	 */
	if (req->zero && !dev->zlp && (length % in->maxpacket) == 0)
		length++;

	req->length = length;

	/* throttle high/super speed IRQ rate back slightly */
	if (gadget_is_dualspeed(dev->gadget))
		req->no_interrupt = (dev->gadget->speed == USB_SPEED_HIGH ||
				     dev->gadget->speed == USB_SPEED_SUPER)
			? ((atomic_read(&dev->tx_qlen) % qmult) != 0)
			: 0;

	retval = usb_ep_queue(in, req, GFP_ATOMIC);
	switch (retval) {
	default:
		DBG(dev, "tx queue err %d\n", retval);
		break;
	case 0:
		net->trans_start = jiffies;
		atomic_inc(&dev->tx_qlen);
	}

	if (retval) {
		dev_kfree_skb_any(skb);
drop:
		dev->net->stats.tx_dropped++;
		spin_lock_irqsave(&dev->req_lock, flags);
		if (list_empty(&dev->tx_reqs))
			netif_start_queue(net);
		list_add(&req->list, &dev->tx_reqs);
		spin_unlock_irqrestore(&dev->req_lock, flags);
	}
	return NETDEV_TX_OK;
}

#else
#define RNDIS_MSG_MAX_SIZE  (1516 + 44)
#define RNDIS_MSG_MAX_QUEUE 32
// RNDIS_MSG_MAX_NUM IP diagrams in the worst case
#define RNDIS_MSG_QUEUE_MAX_SIZE RNDIS_MSG_MAX_NUM*RNDIS_MSG_MAX_SIZE

#define RNDIS_QUEUE_IDLE    1
#define RNDIS_QUEUE_GATHER  2
#define RNDIS_QUEUE_FULL    3
#define RNDIS_QUEUE_SEND    4

#define FULL_BIT_MAP        ((1<<RNDIS_MSG_MAX_NUM)-1)

#define idx_add_one(n)   (((n) + 1) % RNDIS_MSG_MAX_QUEUE)

static int debug_enabled = 0;

struct pkt_msg{
    u32 state;
    u32 len;
    u32 pkt_cnt;
    u32 q_idx;
    void *req;
    void *skbs[RNDIS_MSG_MAX_NUM];
    u32  skb_idx;
    u8 *data;
    u32  bit_map;
    u8 content[RNDIS_MSG_QUEUE_MAX_SIZE+32];
};
struct rndis_msg{
    struct eth_dev* dev;
    spinlock_t buffer_lock;
    u32 w_idx;
    u32 r_idx;
    u32  flow_stop;
	u32	 last_sent;
	u32	 last_complete;
    struct pkt_msg q[RNDIS_MSG_MAX_QUEUE];
};
static struct rndis_msg s_rndis_msg;

static void tx_complete(struct usb_ep *ep, struct usb_request *req);

static inline struct rndis_msg* get_rndis_msg(void)
{
    return &s_rndis_msg;
}

static inline void pkt_msg_clean(struct pkt_msg *q)
{
    int i;
    struct sk_buff* skb;

    q->len = 0;
    q->pkt_cnt = 0;
    q->req = NULL;
    q->skb_idx = 0;
    q->bit_map = 0;
    for(i=0;i<sizeof(q->skbs)/sizeof(q->skbs[0]);i++){
        skb = (struct sk_buff*)q->skbs[i];
        q->skbs[i] = NULL;
        if(skb == NULL)
            continue;
        dev_kfree_skb_any(skb);
    }
    q->state = RNDIS_QUEUE_IDLE;
    q->data =(u8 *)((((u32)q->content)+31)/32*32);
}

static void rndis_msg_init(struct eth_dev* dev)
{
    struct rndis_msg* msg = get_rndis_msg();
    u32 i;

    msg->dev = dev;
    spin_lock_init(&msg->buffer_lock);
    msg->w_idx = 0;
    msg->r_idx = 0;
    for(i = 0; i < RNDIS_MSG_MAX_QUEUE; i++){
        msg->q[i].q_idx = i;
        memset(msg->q[i].skbs,0,sizeof(msg->q[i].skbs));
        pkt_msg_clean(msg->q + i);
    }
}

static void pkt_msg_send(struct rndis_msg *msg, struct pkt_msg *q,
                                struct net_device *net)
{
    struct eth_dev		*dev = netdev_priv(net);
    struct usb_ep		*in = dev->port_usb->in_ep;
    struct usb_request  *req = NULL;
    unsigned long   flags;
    int retval;
    int	length ;
    int i;
    struct sk_buff* skb;

    req = q->req;
    req->context = q;
    req->complete = tx_complete;
	/* NCM requires no zlp if transfer is dwNtbInMaxSize */
    if(q->skb_idx > 1){
        for(i=0;i<q->skb_idx;i++){
            skb = q->skbs[i];
            memcpy(&q->data[q->len],skb->data,skb->len);
            q->len += skb->len;
        }
        req->buf = q->data;
        length = q->len;
    }else{
        skb = q->skbs[0];
        req->buf = skb->data;
        length = skb->len;
    }
    if(debug_enabled)
        printk("pkt_msg_send: quque[%d].len = %d map=0x%x\n",q->q_idx,length,q->bit_map);
    q->bit_map = 0;
    if (dev->port_usb->is_fixed &&
        length == dev->port_usb->fixed_in_len &&
        (length % in->maxpacket) == 0)
        req->zero = 0;
    else
        req->zero = 1;

	/* use zlp framing on tx for strict CDC-Ether conformance,
	 * though any robust network rx path ignores extra padding.
	 * and some hardware doesn't like to write zlps.
	 */
    if (req->zero && !dev->zlp && (length % in->maxpacket) == 0)
        length++;

    req->length = length;
    //printk("pkt_msg_send: %p[%d] %i !\n",req->context,q->q_idx, q->pkt_cnt);
    /* throttle high/super speed IRQ rate back slightly*/
    if (gadget_is_dualspeed(dev->gadget))
        req->no_interrupt = (dev->gadget->speed == USB_SPEED_HIGH ||
                            dev->gadget->speed == USB_SPEED_SUPER)
                ? ((atomic_read(&dev->tx_qlen) % qmult) != 0)
                : 0;

    retval = usb_ep_queue(in, req, GFP_ATOMIC);
    switch (retval) {
    default:
        printk("tx queue err %d\n", retval);
        break;
    case 0:

        net->trans_start = jiffies;
        atomic_inc(&dev->tx_qlen);
    }
    if (retval){
        printk("rndis gather, %i IP diagrams are lost! eth_start_xmit\n", q->pkt_cnt);
        dev->net->stats.tx_dropped += q->skb_idx;
        pkt_msg_clean(q);
        spin_lock_irqsave(&dev->req_lock, flags);
        if (list_empty(&dev->tx_reqs)){
            printk("tx_complete, netif_start_queue1 [%d,%d],[%d,%d]\n", msg->w_idx,msg->r_idx,msg->last_sent,msg->last_complete);
            netif_start_queue(dev->net);
            msg->flow_stop= 0;
        }
        list_add(&req->list, &dev->tx_reqs);
        spin_unlock_irqrestore(&dev->req_lock, flags);
    }
}

static void tx_complete(struct usb_ep *ep, struct usb_request *req)
{

	struct pkt_msg  *q = req->context;
	struct eth_dev	*dev = ep->driver_data;
	struct rndis_msg    *msg = get_rndis_msg();
	struct pkt_msg* w_q;
	//int next = idx_add_one(msg->w_idx);

	switch (req->status) {
	default:
		dev->net->stats.tx_errors++;
		VDBG(dev, "tx err %d\n", req->status);
		/* FALLTHROUGH */
	case -ECONNRESET:		/* unlink */
	case -ESHUTDOWN:		/* disconnect etc */
		break;
	case 0:
		dev->net->stats.tx_bytes += q->len;
	}
	dev->net->stats.tx_packets += q->skb_idx;
    if(debug_enabled)
        printk("tx_complete :[send=%d,r=%d,w=%d] tx_qlen = %d\n", q->q_idx,msg->r_idx,msg->w_idx,atomic_read(&dev->tx_qlen));

    if(req != q->req)
        printk("tx_complete error: req != q->req [%p,%p]\n", req,q->req);

	msg->last_complete = q->q_idx;
    pkt_msg_clean(q);
    if(q->q_idx != msg->w_idx)
        msg->r_idx = idx_add_one(q->q_idx);

	atomic_dec(&dev->tx_qlen);

	if (netif_carrier_ok(dev->net))
		netif_wake_queue(dev->net);
	spin_lock(&dev->req_lock);
	list_add(&req->list, &dev->tx_reqs);
	spin_unlock(&dev->req_lock);
	spin_lock(&msg->buffer_lock);
	w_q = msg->q + msg->w_idx;
	if( msg->w_idx == msg->r_idx &&
		w_q->state == RNDIS_QUEUE_GATHER &&
		w_q->skb_idx == w_q->pkt_cnt){
		msg->last_sent = w_q->q_idx;
		w_q->state = RNDIS_QUEUE_SEND;
		spin_unlock(&msg->buffer_lock);
		pkt_msg_send(msg,w_q,dev->net);
		return;
	}
	spin_unlock(&msg->buffer_lock);

}


static netdev_tx_t eth_alloc_req(struct net_device *net,
    struct usb_request  **req,struct pkt_msg **w_q,struct sk_buff *skb)
{
    unsigned long		flags;
    struct eth_dev		*dev= netdev_priv(net);
    struct rndis_msg    *msg = get_rndis_msg();
    struct pkt_msg *q;

    spin_lock_irqsave(&msg->buffer_lock, flags);
    q = msg->q + msg->w_idx;
    if(q->state == RNDIS_QUEUE_GATHER){
        int next;
        if(debug_enabled)
            printk("eth_alloc_req: quque[%d].skb[%d](%p)->len = %d\n",q->q_idx,q->skb_idx,skb,skb->len);
        q->skbs[q->pkt_cnt] = skb;
        q->pkt_cnt++;
        *w_q = q;
        *req = q->req;
        if(q->pkt_cnt >=RNDIS_MSG_MAX_NUM){
            q->state = RNDIS_QUEUE_FULL;
            next = idx_add_one(msg->w_idx);
            if(msg->r_idx == next){
                netif_stop_queue(dev->net);
                msg->flow_stop= 1;
            } else
                msg->w_idx = next;
        }
        spin_unlock_irqrestore(&msg->buffer_lock, flags);
        return NETDEV_TX_OK;
    }
    spin_unlock_irqrestore(&msg->buffer_lock, flags);
	*w_q = NULL;
	spin_lock_irqsave(&dev->req_lock, flags);
	/*
	 * this freelist can be empty if an interrupt triggered disconnect()
	 * and reconfigured the gadget (shutting down this queue) after the
	 * network stack decided to xmit but before we got the spinlock.
	 */
	if (list_empty(&dev->tx_reqs)) {
		spin_unlock_irqrestore(&dev->req_lock, flags);
        *req = NULL;
		return NETDEV_TX_BUSY;
	}

	*req = container_of(dev->tx_reqs.next, struct usb_request, list);
	list_del(&(*req)->list);

	/* temporarily stop TX queue when the freelist empties */
	if (list_empty(&dev->tx_reqs)){
      //		 printk("eth_alloc_req, netif_stop_queue [%d,%d]\n", msg->w_idx,msg->r_idx);
		netif_stop_queue(net);
        msg->flow_stop= 1;
	}
	spin_unlock_irqrestore(&dev->req_lock, flags);
    return NETDEV_TX_OK;
}

int save_to_queue(struct eth_dev  *dev,struct rndis_msg    *msg,
	struct sk_buff *skb, struct usb_request	*req,struct pkt_msg      **q)
{
    unsigned long flags;
    u32 next =0;
    struct pkt_msg  *w_q;

    spin_lock_irqsave(&msg->buffer_lock, flags);
    w_q = msg->q + msg->w_idx;
    next = idx_add_one(msg->w_idx);
    if(msg->r_idx == next){
        printk("save_to_queue, netif_stop_queue [%d,%d]\n", msg->w_idx,msg->r_idx);
        spin_unlock_irqrestore(&msg->buffer_lock, flags);
        return 1;
    }

    if(w_q->state >= RNDIS_QUEUE_FULL){
        msg->w_idx = next;
        w_q = msg->q + msg->w_idx;
    }

    if(w_q->state == RNDIS_QUEUE_IDLE)
        w_q->req = req;
    else if(w_q->req != req){
        unsigned long flag;
        spin_lock_irqsave(&dev->req_lock, flag);
	    if (list_empty(&dev->tx_reqs))
		    netif_start_queue(dev->net);
		list_add(&req->list, &dev->tx_reqs);
        spin_unlock_irqrestore(&dev->req_lock, flag);
    }

    if(w_q->pkt_cnt < RNDIS_MSG_MAX_NUM){
        if(debug_enabled)
            printk("save_to_queue:quque[%d].skbs[%d](%p).len = %d\n",w_q->q_idx,w_q->skb_idx,skb,skb->len);

        w_q->skbs[w_q->skb_idx] = skb;
        w_q->bit_map |= (1<<w_q->skb_idx);
        w_q->skb_idx++;
        w_q->pkt_cnt++;
        *q = w_q;
    }
    if(w_q->pkt_cnt >= RNDIS_MSG_MAX_NUM){
        w_q->state = RNDIS_QUEUE_FULL;
        next = idx_add_one(msg->w_idx);
        if(next == msg->r_idx){
            netif_stop_queue(dev->net);
            msg->flow_stop= 1;
        }else
            msg->w_idx = next;

    }else{
        w_q->state = RNDIS_QUEUE_GATHER;
    }
    spin_unlock_irqrestore(&msg->buffer_lock, flags);
    return 0;
}
static void update_sbks_in_queue(struct pkt_msg *q,struct  sk_buff *skb_new,struct sk_buff *skb_old)
{
    int i;
    unsigned long flags;
    struct rndis_msg    *msg = get_rndis_msg();

    spin_lock_irqsave(&msg->buffer_lock, flags);
    for(i=0;i<q->pkt_cnt;i++){
        if(q->skbs[i] == skb_old){
            q->skbs[i] = skb_new;
            q->bit_map |= (1<<i);
            q->skb_idx++;
            if(debug_enabled)
                printk("update_sbks:quque[%d].skbs[%d](%p,%p).len = %d\n",q->q_idx,i,skb_old,skb_new,skb_new->len);
            break;
        }
    }
    spin_unlock_irqrestore(&msg->buffer_lock, flags);
}
static netdev_tx_t eth_start_xmit(struct sk_buff *skb,
					struct net_device *net)
{
	struct eth_dev		*dev = netdev_priv(net);
	int			length = skb->len;
	int			retval=0;
	struct usb_request	*req = NULL;
	unsigned long		flags;
	struct usb_ep		*in;
	u16			cdc_filter;
    struct rndis_msg    *msg = get_rndis_msg();
    struct pkt_msg	*w_q = NULL;
    struct sk_buff *skb_old;

	spin_lock_irqsave(&dev->lock, flags);
	if (dev->port_usb) {
		in = dev->port_usb->in_ep;
		cdc_filter = dev->port_usb->cdc_filter;
	} else {
		in = NULL;
		cdc_filter = 0;
	}
	spin_unlock_irqrestore(&dev->lock, flags);

	if (!in) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	/* apply outgoing CDC or RNDIS filters */
	if (!is_promisc(cdc_filter)) {
		u8		*dest = skb->data;

		if (is_multicast_ether_addr(dest)) {
			u16	type;

			/* ignores USB_CDC_PACKET_TYPE_MULTICAST and host
			 * SET_ETHERNET_MULTICAST_FILTERS requests
			 */
			if (is_broadcast_ether_addr(dest))
				type = USB_CDC_PACKET_TYPE_BROADCAST;
			else
				type = USB_CDC_PACKET_TYPE_ALL_MULTICAST;
			if (!(cdc_filter & type)) {
				dev_kfree_skb_any(skb);
				return NETDEV_TX_OK;
			}
		}
		/* ignores USB_CDC_PACKET_TYPE_DIRECTED */
	}
    skb_old=skb;
    eth_alloc_req(net,&req,&w_q,skb);
    if(req == NULL)
        return NETDEV_TX_BUSY;
	/* no buffer copies needed, unless the network stack did it
	 * or the hardware can't use skb buffers.
	 * or there's not enough space for extra headers we need
	 */
	if (dev->wrap) {
		unsigned long	flags;

		spin_lock_irqsave(&dev->lock, flags);

		if (dev->port_usb)
			skb = dev->wrap(dev->port_usb, skb);
		spin_unlock_irqrestore(&dev->lock, flags);
		if (!skb)
			goto drop;

		length = skb->len;
	}
	/*
	 * Align data to 32bit if the dma controller requires it
	 */
	if (gadget_dma32(dev->gadget)) {
		unsigned long align = (unsigned long)skb->data & 3;
		if (WARN_ON(skb_headroom(skb) < align)) {
			dev_kfree_skb_any(skb);
			goto drop;
		} else if (align) {
			u8 *data = skb->data;
			size_t len = skb_headlen(skb);
			skb->data -= align;
			memmove(skb->data, data, len);
			skb_set_tail_pointer(skb, len);
		}
	}

    retval = 0;
    if(w_q==NULL)
		retval = save_to_queue(dev,msg,skb,req,&w_q);
	else if(skb!=skb_old)
		update_sbks_in_queue(w_q,skb,skb_old);

    if(retval == 0){
        spin_lock_irqsave(&msg->buffer_lock, flags);
        if(w_q && w_q->bit_map==FULL_BIT_MAP && (w_q->state != RNDIS_QUEUE_SEND)){
            msg->last_sent = w_q->q_idx;
            w_q->state = RNDIS_QUEUE_SEND;
            spin_unlock_irqrestore(&msg->buffer_lock, flags);
            pkt_msg_send(msg,w_q,net);
            return NETDEV_TX_OK;
        }

        w_q = msg->q + msg->w_idx;
        if(w_q->bit_map==FULL_BIT_MAP && w_q->state != RNDIS_QUEUE_SEND){
            msg->last_sent = w_q->q_idx;
            w_q->state = RNDIS_QUEUE_SEND;
            spin_unlock_irqrestore(&msg->buffer_lock, flags);
            pkt_msg_send(msg,w_q,net);
            return NETDEV_TX_OK;
        }
        if( msg->w_idx == msg->r_idx &&
            w_q->state == RNDIS_QUEUE_GATHER &&
            w_q->skb_idx == w_q->pkt_cnt){
			msg->last_sent = w_q->q_idx;
            w_q->state = RNDIS_QUEUE_SEND;
            spin_unlock_irqrestore(&msg->buffer_lock, flags);
            pkt_msg_send(msg,w_q,net);
            return NETDEV_TX_OK;
        }
        spin_unlock_irqrestore(&msg->buffer_lock, flags);
        return NETDEV_TX_OK;
    }
	if (retval) {
		dev_kfree_skb_any(skb);
drop:
        printk("drop req\n");
		dev->net->stats.tx_dropped++;
		spin_lock_irqsave(&dev->req_lock, flags);
		if (list_empty(&dev->tx_reqs))
			netif_start_queue(net);
		list_add(&req->list, &dev->tx_reqs);
		spin_unlock_irqrestore(&dev->req_lock, flags);
	}
	return NETDEV_TX_OK;
}
#endif
/*-------------------------------------------------------------------------*/

static void eth_start(struct eth_dev *dev, gfp_t gfp_flags)
{
	DBG(dev, "%s\n", __func__);

	/* fill the rx queue */
	rx_fill(dev, gfp_flags);

	/* and open the tx floodgates */
	atomic_set(&dev->tx_qlen, 0);
	netif_wake_queue(dev->net);
#ifdef CONFIG_USB_SPRD_DWC
    rndis_msg_init(dev);
#endif
}

static int eth_open(struct net_device *net)
{
	struct eth_dev	*dev = netdev_priv(net);
	struct gether	*link;

	DBG(dev, "%s\n", __func__);
	if (netif_carrier_ok(dev->net))
		eth_start(dev, GFP_KERNEL);

	spin_lock_irq(&dev->lock);
	link = dev->port_usb;
	if (link && link->open)
		link->open(link);
	spin_unlock_irq(&dev->lock);

	return 0;
}

static int eth_stop(struct net_device *net)
{
	struct eth_dev	*dev = netdev_priv(net);
	unsigned long	flags;

	VDBG(dev, "%s\n", __func__);
	netif_stop_queue(net);

	DBG(dev, "stop stats: rx/tx %ld/%ld, errs %ld/%ld\n",
		dev->net->stats.rx_packets, dev->net->stats.tx_packets,
		dev->net->stats.rx_errors, dev->net->stats.tx_errors
		);

	/* ensure there are no more active requests */
	spin_lock_irqsave(&dev->lock, flags);
	if (dev->port_usb) {
		struct gether	*link = dev->port_usb;
		const struct usb_endpoint_descriptor *in;
		const struct usb_endpoint_descriptor *out;

		if (link->close)
			link->close(link);

		/* NOTE:  we have no abort-queue primitive we could use
		 * to cancel all pending I/O.  Instead, we disable then
		 * reenable the endpoints ... this idiom may leave toggle
		 * wrong, but that's a self-correcting error.
		 *
		 * REVISIT:  we *COULD* just let the transfers complete at
		 * their own pace; the network stack can handle old packets.
		 * For the moment we leave this here, since it works.
		 */
		in = link->in_ep->desc;
		out = link->out_ep->desc;
		usb_ep_disable(link->in_ep);
		usb_ep_disable(link->out_ep);
		if (netif_carrier_ok(net)) {
			DBG(dev, "host still using in/out endpoints\n");
			link->in_ep->desc = in;
			link->out_ep->desc = out;
			usb_ep_enable(link->in_ep);
			usb_ep_enable(link->out_ep);
		}
	}
	spin_unlock_irqrestore(&dev->lock, flags);

	return 0;
}

/*-------------------------------------------------------------------------*/

/* initial value, changed by "ifconfig usb0 hw ether xx:xx:xx:xx:xx:xx" */
static char *dev_addr;
module_param(dev_addr, charp, S_IRUGO);
MODULE_PARM_DESC(dev_addr, "Device Ethernet Address");

/* this address is invisible to ifconfig */
static char *host_addr;
module_param(host_addr, charp, S_IRUGO);
MODULE_PARM_DESC(host_addr, "Host Ethernet Address");

static int get_ether_addr(const char *str, u8 *dev_addr)
{
	if (str) {
		unsigned	i;

		for (i = 0; i < 6; i++) {
			unsigned char num;

			if ((*str == '.') || (*str == ':'))
				str++;
			num = hex_to_bin(*str++) << 4;
			num |= hex_to_bin(*str++);
			dev_addr [i] = num;
		}
		if (is_valid_ether_addr(dev_addr))
			return 0;
	}
	eth_random_addr(dev_addr);
	return 1;
}

static const struct net_device_ops eth_netdev_ops = {
	.ndo_open		= eth_open,
	.ndo_stop		= eth_stop,
	.ndo_start_xmit		= eth_start_xmit,
	.ndo_change_mtu		= ueth_change_mtu,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static struct device_type gadget_type = {
	.name	= "gadget",
};

/**
 * gether_setup_name - initialize one ethernet-over-usb link
 * @g: gadget to associated with these links
 * @ethaddr: NULL, or a buffer in which the ethernet address of the
 *	host side of the link is recorded
 * @netname: name for network device (for example, "usb")
 * Context: may sleep
 *
 * This sets up the single network link that may be exported by a
 * gadget driver using this framework.  The link layer addresses are
 * set up using module parameters.
 *
 * Returns negative errno, or zero on success
 */
struct eth_dev *gether_setup_name(struct usb_gadget *g, u8 ethaddr[ETH_ALEN],
		const char *netname)
{
	struct eth_dev		*dev;
	struct net_device	*net;
	int			status;

	net = alloc_etherdev(sizeof *dev);
	if (!net)
		return ERR_PTR(-ENOMEM);

	dev = netdev_priv(net);
	spin_lock_init(&dev->lock);
	spin_lock_init(&dev->req_lock);
	INIT_WORK(&dev->work, eth_work);
	INIT_LIST_HEAD(&dev->tx_reqs);
	INIT_LIST_HEAD(&dev->rx_reqs);

	skb_queue_head_init(&dev->rx_frames);

	/* network device setup */
	dev->net = net;
	snprintf(net->name, sizeof(net->name), "%s%%d", netname);

	if (get_ether_addr(dev_addr, net->dev_addr))
		dev_warn(&g->dev,
			"using random %s ethernet address\n", "self");
	if (get_ether_addr(host_addr, dev->host_mac))
		dev_warn(&g->dev,
			"using random %s ethernet address\n", "host");

	if (ethaddr)
		memcpy(ethaddr, dev->host_mac, ETH_ALEN);

	net->netdev_ops = &eth_netdev_ops;

	SET_ETHTOOL_OPS(net, &ops);

	dev->gadget = g;
	SET_NETDEV_DEV(net, &g->dev);
	SET_NETDEV_DEVTYPE(net, &gadget_type);

	status = register_netdev(net);
	if (status < 0) {
		dev_dbg(&g->dev, "register_netdev failed, %d\n", status);
		free_netdev(net);
		dev = ERR_PTR(status);
	} else {
		INFO(dev, "MAC %pM\n", net->dev_addr);
		INFO(dev, "HOST MAC %pM\n", dev->host_mac);

		/* two kinds of host-initiated state changes:
		 *  - iff DATA transfer is active, carrier is "on"
		 *  - tx queueing enabled if open *and* carrier is "on"
		 */
		netif_carrier_off(net);
	}

	return dev;
}

/**
 * gether_cleanup - remove Ethernet-over-USB device
 * Context: may sleep
 *
 * This is called to free all resources allocated by @gether_setup().
 */
void gether_cleanup(struct eth_dev *dev)
{
	if (!dev)
		return;

	unregister_netdev(dev->net);
	flush_work(&dev->work);
	free_netdev(dev->net);
}

/**
 * gether_connect - notify network layer that USB link is active
 * @link: the USB link, set up with endpoints, descriptors matching
 *	current device speed, and any framing wrapper(s) set up.
 * Context: irqs blocked
 *
 * This is called to activate endpoints and let the network layer know
 * the connection is active ("carrier detect").  It may cause the I/O
 * queues to open and start letting network packets flow, but will in
 * any case activate the endpoints so that they respond properly to the
 * USB host.
 *
 * Verify net_device pointer returned using IS_ERR().  If it doesn't
 * indicate some error code (negative errno), ep->driver_data values
 * have been overwritten.
 */
struct net_device *gether_connect(struct gether *link)
{
	struct eth_dev		*dev = link->ioport;
	int			result = 0;

	if (!dev)
		return ERR_PTR(-EINVAL);

	link->in_ep->driver_data = dev;
	result = usb_ep_enable(link->in_ep);
	if (result != 0) {
		DBG(dev, "enable %s --> %d\n",
			link->in_ep->name, result);
		goto fail0;
	}

	link->out_ep->driver_data = dev;
	result = usb_ep_enable(link->out_ep);
	if (result != 0) {
		DBG(dev, "enable %s --> %d\n",
			link->out_ep->name, result);
		goto fail1;
	}

	if (result == 0)
		result = alloc_requests(dev, link, qlen(dev->gadget));

	if (result == 0) {
		dev->zlp = link->is_zlp_ok;
		DBG(dev, "qlen %d\n", qlen(dev->gadget));

		dev->header_len = link->header_len;
		dev->unwrap = link->unwrap;
		dev->wrap = link->wrap;

		spin_lock(&dev->lock);
		dev->port_usb = link;
		if (netif_running(dev->net)) {
			if (link->open)
				link->open(link);
		} else {
			if (link->close)
				link->close(link);
		}
		spin_unlock(&dev->lock);

		netif_carrier_on(dev->net);
		if (netif_running(dev->net))
			eth_start(dev, GFP_ATOMIC);

	/* on error, disable any endpoints  */
	} else {
		(void) usb_ep_disable(link->out_ep);
fail1:
		(void) usb_ep_disable(link->in_ep);
	}
fail0:
	/* caller is responsible for cleanup on error */
	if (result < 0)
		return ERR_PTR(result);
	return dev->net;
}

/**
 * gether_disconnect - notify network layer that USB link is inactive
 * @link: the USB link, on which gether_connect() was called
 * Context: irqs blocked
 *
 * This is called to deactivate endpoints and let the network layer know
 * the connection went inactive ("no carrier").
 *
 * On return, the state is as if gether_connect() had never been called.
 * The endpoints are inactive, and accordingly without active USB I/O.
 * Pointers to endpoint descriptors and endpoint private data are nulled.
 */
void gether_disconnect(struct gether *link)
{
	struct eth_dev		*dev = link->ioport;
	struct usb_request	*req;

	WARN_ON(!dev);
	if (!dev)
		return;

	DBG(dev, "%s\n", __func__);

	netif_stop_queue(dev->net);
	netif_carrier_off(dev->net);

	/* disable endpoints, forcing (synchronous) completion
	 * of all pending i/o.  then free the request objects
	 * and forget about the endpoints.
	 */
	usb_ep_disable(link->in_ep);
	spin_lock(&dev->req_lock);
	while (!list_empty(&dev->tx_reqs)) {
		req = container_of(dev->tx_reqs.next,
					struct usb_request, list);
		list_del(&req->list);

		spin_unlock(&dev->req_lock);
		usb_ep_free_request(link->in_ep, req);
		spin_lock(&dev->req_lock);
	}
	spin_unlock(&dev->req_lock);
	link->in_ep->driver_data = NULL;
	link->in_ep->desc = NULL;

	usb_ep_disable(link->out_ep);
	spin_lock(&dev->req_lock);
	while (!list_empty(&dev->rx_reqs)) {
		req = container_of(dev->rx_reqs.next,
					struct usb_request, list);
		list_del(&req->list);

		spin_unlock(&dev->req_lock);
		usb_ep_free_request(link->out_ep, req);
		spin_lock(&dev->req_lock);
	}
	spin_unlock(&dev->req_lock);
	link->out_ep->driver_data = NULL;
	link->out_ep->desc = NULL;

	/* finish forgetting about this USB link episode */
	dev->header_len = 0;
	dev->unwrap = NULL;
	dev->wrap = NULL;

	spin_lock(&dev->lock);
	dev->port_usb = NULL;
	spin_unlock(&dev->lock);
}
