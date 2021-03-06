/*
 *  Copyright (C) 2012  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
/*
 * (c) copyright 1987 by the Vrije Universiteit, Amsterdam, The Netherlands.
 * See the copyright notice in the ACK home directory, in the file "Copyright".
 */
/*
 * rtl8139.c
 *
 * This file contains a ethernet device driver for Realtek rtl8139 based
 * ethernet cards.
 *
 * The valid messages and their parameters are:
 *
 *   m_type	  DL_PORT    DL_PROC   DL_COUNT   DL_MODE   DL_ADDR   DL_GRANT
 * |------------+----------+---------+----------+---------+---------+---------|
 * | HARDINT	|          |         |          |         |         |	      |
 * |------------|----------|---------|----------|---------|---------|---------|
 * | DL_WRITE	| port nr  | proc nr | count    | mode    | address |	      |
 * |------------|----------|---------|----------|---------|---------|---------|
 * | DL_WRITEV	| port nr  | proc nr | count    | mode    | address |	      |
 * |------------|----------|---------|----------|---------|---------|---------|
 * | DL_WRITEV_S| port nr  | proc nr | count    | mode    |	    |  grant  |
 * |------------|----------|---------|----------|---------|---------|---------|
 * | DL_READ	| port nr  | proc nr | count    |         | address |	      |
 * |------------|----------|---------|----------|---------|---------|---------|
 * | DL_READV	| port nr  | proc nr | count    |         | address |	      |
 * |------------|----------|---------|----------|---------|---------|---------|
 * | DL_READV_S	| port nr  | proc nr | count    |         |	    |  grant  |
 * |------------|----------|---------|----------|---------|---------|---------|
 * | DL_CONF	| port nr  | proc nr |		| mode    | address |	      |
 * |------------|----------|---------|----------|---------|---------|---------|
 * | DL_GETSTAT	| port nr  | proc nr |          |         | address |	      |
 * |------------|----------|---------|----------|---------|---------|---------|
 * |DL_GETSTAT_S| port nr  | proc nr |          |         |	    |  grant  |
 * |------------|----------|---------|----------|---------|---------|---------|
 * | DL_STOP	| port_nr  |         |          |         |	    |	      |
 * |------------|----------|---------|----------|---------|---------|---------|
 *
 * The messages sent are:
 *
 *   m_type	  DL_PORT    DL_PROC   DL_COUNT   DL_STAT   DL_CLCK
 * |------------|----------|---------|----------|---------|---------|
 * |DL_TASK_REPL| port nr  | proc nr | rd-count | err|stat| clock   |
 * |------------|----------|---------|----------|---------|---------|
 *
 *   m_type	  m_data1     m_data2       m_data3|m_data4
 * |------------|---------|-----------|---------------------|
 * |DL_CONF_REPL| port nr | last port | ethernet addr       |
 * |------------|---------|-----------|---------------------|
 *
 *   m_type	  DL_PORT    DL_STAT       
 * |------------|---------|-----------|
 * |DL_STAT_REPL| port nr |   err     |
 * |------------|---------|-----------|
 *
 * Created:	Aug 2003 by Philip Homburg <philip@cs.vu.nl>
 * Changes:
 *   Aug 15, 2004   sync alarms replace watchdogs timers  (Jorrit N. Herder)
 *   May 02, 2004   flag alarms replace micro_elapsed()  (Jorrit N. Herder)
 *
 */

#include <nucleos/drivers.h>

#include <stdlib.h>
#include <stdio.h>
#include <nucleos/string.h>
#include <nucleos/stddef.h>
#include <nucleos/com.h>
#include <servers/ds/ds.h>
#include <nucleos/keymap.h>
#include <nucleos/syslib.h>
#include <nucleos/type.h>
#include <nucleos/sysutil.h>
#include <nucleos/endpoint.h>
#include <nucleos/timer.h>
#include <net/ether.h>
#include <net/eth_io.h>
#include <ibm/pci.h>

#include <nucleos/types.h>
#include <nucleos/fcntl.h>
#include <assert.h>
#include <nucleos/unistd.h>
#include <kernel/const.h>
#include <kernel/types.h>
#include <asm/ioctls.h>

#define tmra_ut			timer_t
#define tmra_inittimer(tp)	tmr_inittimer(tp)
#define Proc_number(p)		proc_number(p)
#define debug			0
#define printW()		((void)0)
#define vm_1phys2bus(p)		(p)

#define VERBOSE		1	/* display message during init */

#include "rtl8139.h"

#define RX_BUFSIZE	RL_RCR_RBLEN_64K_SIZE
#define RX_BUFBITS	RL_RCR_RBLEN_64K
#define N_TX_BUF	RL_N_TX

#define RE_PORT_NR	1		/* Minix */

/* I/O vectors are handled IOVEC_NR entries at a time. */
#define IOVEC_NR	16

/* Configuration */
#define RL_ENVVAR	"RTLETH"

static struct pcitab
{
	u16_t vid;
	u16_t did;
	int checkclass;
} pcitab[]=
{
	{ 0x10ec, 0x8139, 0 },	/* Realtek RTL8139 */

	/* Alternative IDs */
	{ 0x02ac, 0x1012, 0 },	/* SpeedStream 1012 PCMCIA 10/100 */
	{ 0x1065, 0x8139, 0 },	/* Texas Microsystems 8139C Network Card */
	{ 0x1113, 0x1211, 0 },	/* Accton MPX5030 or SMC1211TX EZCard 10/100 */
	{ 0x1186, 0x1300, 0 },	/* D-Link DFE530TX+/DFE538TX */
	{ 0x1186, 0x1340, 0 },	/* D-Link DFE690TXD */
	{ 0x11db, 0x1234, 0 },	/* Sega Dreamcast HIT-400 */
	{ 0x1259, 0xa117, 0 },	/* Allied Telesyn 8139 */
	{ 0x1259, 0xa11e, 0 },	/* Allied Telesyn 8139 */
	{ 0x126c, 0x1211, 0 },	/* Northern Telecom 10/100BaseTX*/
	{ 0x13d1, 0xab06, 0 },	/* AboCom FE2000VX */
	{ 0x1432, 0x9130, 0 },	/* Edimax Computer Co. RTL81xx */
	{ 0x14ea, 0xab06, 0 },	/* Planex FNW-3603-TX */
	{ 0x14ea, 0xab07, 0 },	/* Planex FNW-3800-TX */
	{ 0x1500, 0x1360, 0 },	/* Delta Electronics RealTek Ethernet */
	{ 0x1743, 0x8139, 0 },	/* Peppercon AG 8139  ROL/F-100 */
	{ 0x4033, 0x1360, 0 },	/* Addtron Technology 8139 */

	{ 0x0000, 0x0000, 0 }
};

typedef struct re
{
	port_t re_base_port;
	int re_irq;
	int re_mode;
	int re_flags;
	int re_client;
	int re_link_up;
	int re_got_int;
	int re_send_int;
	int re_report_link;
	int re_clear_rx;
	int re_need_reset;
	int re_tx_alive;
	char *re_model;

	/* Rx */
	phys_bytes re_rx_buf;
	char  *v_re_rx_buf;
	vir_bytes re_read_s;

	/* Tx */
	int re_tx_head;
	int re_tx_tail;
	struct
	{
		int ret_busy;
		phys_bytes ret_buf;
		char * v_ret_buf;
	} re_tx[N_TX_BUF];
	u32_t re_ertxth;	/* Early Tx Threshold */

	/* PCI related */
	int re_seen;			/* TRUE iff device available */
	u8_t re_pcibus;	
	u8_t re_pcidev;	
	u8_t re_pcifunc;	

	/* 'large' items */
	int re_hook_id;			/* IRQ hook id at kernel */
	eth_stat_t re_stat;
	ether_addr_t re_address;
	kipc_msg_t re_rx_mess;
	kipc_msg_t re_tx_mess;
	char re_name[sizeof("rtl8139#n")];
	iovec_t re_iovec[IOVEC_NR];
	iovec_s_t re_iovec_s[IOVEC_NR];
}
re_t;

#define REM_DISABLED	0x0
#define REM_ENABLED	0x1

#define REF_PACK_SENT	0x001
#define REF_PACK_RECV	0x002
#define REF_SEND_AVAIL	0x004
#define REF_READING	0x010
#define REF_EMPTY	0x000
#define REF_PROMISC	0x040
#define REF_MULTI	0x080
#define REF_BROAD	0x100
#define REF_ENABLED	0x200

static re_t re_table[RE_PORT_NR];

static u16_t eth_ign_proto;
static tmra_ut rl_watchdog;

static unsigned my_inb(u16 port);
static unsigned my_inw(u16 port);
static unsigned my_inl(u16 port);

static unsigned my_inb(u16 port) {
	u32_t value;
	int s;
	if ((s=sys_inb(port, (unsigned long*)&value)) != 0)
		printk("RTL8139: warning, sys_inb failed: %d\n", s);
	return value;
}
static unsigned my_inw(u16 port) {
	u32_t value;
	int s;
	if ((s=sys_inw(port, (unsigned long*)&value)) != 0)
		printk("RTL8139: warning, sys_inw failed: %d\n", s);
	return value;
}
static unsigned my_inl(u16 port) {
	u32 value;
	int s;
	if ((s=sys_inl(port, (unsigned long*)&value)) != 0)
		printk("RTL8139: warning, sys_inl failed: %d\n", s);
	return value;
}
#define rl_inb(port, offset)	(my_inb((port) + (offset)))
#define rl_inw(port, offset)	(my_inw((port) + (offset)))
#define rl_inl(port, offset)	(my_inl((port) + (offset)))

static void my_outb(u16 port, u8 value);
static void my_outw(u16 port, u16 value);
static void my_outl(u16 port, u32 value);

static void my_outb(u16 port, u8 value) {
	int s;
	if ((s=sys_outb(port, value)) != 0)
		printk("RTL8139: warning, sys_outb failed: %d\n", s);
}
static void my_outw(u16 port, u16 value) {
	int s;
	if ((s=sys_outw(port, value)) != 0)
		printk("RTL8139: warning, sys_outw failed: %d\n", s);
}
static void my_outl(u16 port, u32 value) {
	int s;
	if ((s=sys_outl(port, value)) != 0)
		printk("RTL8139: warning, sys_outl failed: %d\n", s);
}
#define rl_outb(port, offset, value)	(my_outb((port) + (offset), (value)))
#define rl_outw(port, offset, value)	(my_outw((port) + (offset), (value)))
#define rl_outl(port, offset, value)	(my_outl((port) + (offset), (value)))

static void rl_init(kipc_msg_t *mp);
static void rl_pci_conf(void);
static int rl_probe(re_t *rep);
static void rl_conf_hw(re_t *rep);
static void rl_init_buf(re_t *rep);
static void rl_init_hw(re_t *rep);
static void rl_reset_hw(re_t *rep);
static void rl_confaddr(re_t *rep);
static void rl_rec_mode(re_t *rep);
static void rl_readv(kipc_msg_t *mp, int from_int, int vectored);
static void rl_readv_s(kipc_msg_t *mp, int from_int);
static void rl_writev(kipc_msg_t *mp, int from_int, int vectored);
static void rl_writev_s(kipc_msg_t *mp, int from_int);
static void rl_check_ints(re_t *rep);
static void rl_report_link(re_t *rep);
static void mii_print_techab(u16 techab);
static void mii_print_stat_speed(u16 stat, u16 extstat);
static void rl_clear_rx(re_t *rep);
static void rl_do_reset(re_t *rep);
static void rl_getstat(kipc_msg_t *mp);
static void rl_getstat_s(kipc_msg_t *mp);
static void rl_getname(kipc_msg_t *mp);
static void reply(re_t *rep, int err, int may_block);
static void mess_reply(kipc_msg_t *req, kipc_msg_t *reply);
static void rtl8139_stop(void);
static void check_int_events(void);
static int do_hard_int(void);
static void rtl8139_dump(kipc_msg_t *m);
#if 0
static void dump_phy(re_t *rep);
#endif
static int rl_handler(re_t *rep);
static void rl_watchdog_f(timer_t *tp);
static void tell_dev(vir_bytes start, size_t size, int pci_bus, int pci_dev, int pci_func);

/* The message used in the main loop is made global, so that rl_watchdog_f()
 * can change its message type to fake an interrupt message.
 */
static kipc_msg_t m;
static int int_event_check;		/* set to TRUE if events arrived */

static char *progname;
extern int errno;
u32_t system_hz;

/*===========================================================================*
 *				main				     *
 *===========================================================================*/
int main(int argc, char *argv[])
{
	int fkeys, sfkeys;
	u32_t inet_proc_nr;
	int i, r;
	re_t *rep;
	long v;

	system_hz = sys_hz();

	(progname=strrchr(argv[0],'/')) ? progname++ : (progname=argv[0]);

	env_setargs(argc, argv);

	v= 0;
	(void) env_parse("ETH_IGN_PROTO", "x", 0, &v, 0x0000L, 0xFFFFL);
	eth_ign_proto= htons((u16_t) v);

	/* Observe some function key for debug dumps. */
	fkeys = sfkeys = 0; bit_set(sfkeys, 9);
	if ((r=fkey_map(&fkeys, &sfkeys)) != 0) 
	    printk("Warning: RTL8139 couldn't observe Shift+F9 key: %d\n",r);

	/* Claim buffer memory now under Minix, before PM_PROC_NR takes it all. */
	for (rep= &re_table[0]; rep < re_table+RE_PORT_NR; rep++)
		rl_init_buf(rep);

	/* Try to notify INET that we are present (again). If INET cannot
	 * be found, assume this is the first time we started and INET is
	 * not yet alive.
	 */
	r= ds_retrieve_u32("inet", &inet_proc_nr);
	if (r == 0)
		kipc_module_call(KIPC_NOTIFY, 0, inet_proc_nr, 0);
	else if (r != -ESRCH)
		printk("rtl8139: ds_retrieve_u32 failed for 'inet': %d\n", r);

	while (TRUE)
	{
		if ((r = kipc_module_call(KIPC_RECEIVE, 0, ENDPT_ANY, &m)) != 0)
			panic("rtl8139","receive failed", r);

		if (is_notify(m.m_type)) {
			switch (_ENDPOINT_P(m.m_source)) {
				case RS_PROC_NR:
					kipc_module_call(KIPC_NOTIFY, 0, m.m_source, 0);
					break;
				case CLOCK:
					/* 
					 * Under MINIX, synchronous alarms are
					 * used instead of watchdog functions.
					 * The approach is very different: MINIX
					 * VMD timeouts are handled within the
					 * kernel (the watchdog is executed by
					 * CLOCK), and notify() the driver in
					 * some cases.  MINIX timeouts result in
					 * a SYN_ALARM message to the driver and
					 * thus are handled where they should be
					 * handled. Locally, watchdog functions
					 * are used again. 
					 */
					rl_watchdog_f(NULL);     
					break;		 
				case HARDWARE:
					do_hard_int();
					if (int_event_check)
						check_int_events();
					break ;
				case TTY_PROC_NR:
					rtl8139_dump(&m);
					break;
				case PM_PROC_NR:
				{
					sigset_t set;

					if (getsigset(&set) != 0) break;

					if (sigismember(&set, SIGTERM))
						rtl8139_stop();

					break;
				}
				default:
					panic("rtl8139","illegal notify from",
								m.m_source);
			}

			/* done, get nwe message */
			continue;
		}

		switch (m.m_type)
		{
		case DL_WRITE:	rl_writev(&m, FALSE, FALSE);	break;
		case DL_WRITEV:	rl_writev(&m, FALSE, TRUE);	break;
		case DL_WRITEV_S: rl_writev_s(&m, FALSE);	break;
		case DL_READ:	rl_readv(&m, FALSE, FALSE);	break;
		case DL_READV:	rl_readv(&m, FALSE, TRUE);	break;
		case DL_READV_S: rl_readv_s(&m, FALSE);		break;
		case DL_CONF:	rl_init(&m);			break;
		case DL_GETSTAT: rl_getstat(&m);		break;
		case DL_GETSTAT_S: rl_getstat_s(&m);		break;
		case DL_GETNAME: rl_getname(&m);		break;
#if 0
		case DL_STOP:	do_stop(&m);			break;
#endif
		default:
			panic("rtl8139","illegal message", m.m_type);
		}
	}
}

/*===========================================================================*
 *				check_int_events			     *
 *===========================================================================*/
static void check_int_events(void) 
{
  int i;
  re_t *rep;
			for (i= 0, rep= &re_table[0]; i<RE_PORT_NR; i++, rep++)
			{
				if (rep->re_mode != REM_ENABLED)
					continue;
				if (!rep->re_got_int)
					continue;
				rep->re_got_int= 0;
				assert(rep->re_flags & REF_ENABLED);
				rl_check_ints(rep);
			}
}

/*===========================================================================*
 *				rtl8139_stop				     *
 *===========================================================================*/
static void rtl8139_stop()
{
	int i;
	re_t *rep;

	for (i= 0, rep= &re_table[0]; i<RE_PORT_NR; i++, rep++)
	{
		if (rep->re_mode != REM_ENABLED)
			continue;
		rl_outb(rep->re_base_port, RL_CR, 0);
	}
	exit(0);
}

/*===========================================================================*
 *				rtl8139_dump				     *
 *===========================================================================*/
static void rtl8139_dump(m)
kipc_msg_t *m;			/* pointer to request message */
{
	re_t *rep;
	int i;

	printk("\n");
	for (i= 0, rep = &re_table[0]; i<RE_PORT_NR; i++, rep++)
	{
		if (rep->re_mode == REM_DISABLED)
			printk("Realtek RTL 8139 port %d is disabled\n", i);

		if (rep->re_mode != REM_ENABLED)
			continue;

		printk("Realtek RTL 8139 statistics of port %d:\n", i);

		printk("recvErr    :%8ld\t", rep->re_stat.ets_recvErr);
		printk("sendErr    :%8ld\t", rep->re_stat.ets_sendErr);
		printk("OVW        :%8ld\n", rep->re_stat.ets_OVW);

		printk("CRCerr     :%8ld\t", rep->re_stat.ets_CRCerr);
		printk("frameAll   :%8ld\t", rep->re_stat.ets_frameAll);
		printk("missedP    :%8ld\n", rep->re_stat.ets_missedP);

		printk("packetR    :%8ld\t", rep->re_stat.ets_packetR);
		printk("packetT    :%8ld\t", rep->re_stat.ets_packetT);
		printk("transDef   :%8ld\n", rep->re_stat.ets_transDef);

		printk("collision  :%8ld\t", rep->re_stat.ets_collision);
		printk("transAb    :%8ld\t", rep->re_stat.ets_transAb);
		printk("carrSense  :%8ld\n", rep->re_stat.ets_carrSense);

		printk("fifoUnder  :%8ld\t", rep->re_stat.ets_fifoUnder);
		printk("fifoOver   :%8ld\t", rep->re_stat.ets_fifoOver);
		printk("CDheartbeat:%8ld\n", rep->re_stat.ets_CDheartbeat);

		printk("OWC        :%8ld\t", rep->re_stat.ets_OWC);

		printk("re_flags = 0x%x\n", rep->re_flags);

		printk(
	"TSAD: 0x%04x, TSD: 0x%08x, 0x%08x, 0x%08x, 0x%08x\n",
			rl_inw(rep->re_base_port, RL_TSAD),
			rl_inl(rep->re_base_port, RL_TSD0+0*4),
			rl_inl(rep->re_base_port, RL_TSD0+1*4),
			rl_inl(rep->re_base_port, RL_TSD0+2*4),
			rl_inl(rep->re_base_port, RL_TSD0+3*4));
		printk("tx_head %d, tx_tail %d, busy: %d %d %d %d\n",
			rep->re_tx_head, rep->re_tx_tail,
			rep->re_tx[0].ret_busy, rep->re_tx[1].ret_busy,
			rep->re_tx[2].ret_busy, rep->re_tx[3].ret_busy);
	}
}

/*===========================================================================*
 *				do_init					     *
 *===========================================================================*/
static void rl_init(mp)
kipc_msg_t *mp;
{
	static int first_time= 1;

	int port;
	re_t *rep;
	kipc_msg_t reply_mess;

	if (first_time)
	{
		first_time= 0;
		rl_pci_conf(); /* Configure PCI devices. */

		tmra_inittimer(&rl_watchdog);
		/* Use a synchronous alarm instead of a watchdog timer. */
		sys_setalarm(system_hz, 0);
	}

	port = mp->DL_PORT;
	if (port < 0 || port >= RE_PORT_NR)
	{
		reply_mess.m_type= DL_CONF_REPLY;
		reply_mess.m_data1= -ENXIO;
		mess_reply(mp, &reply_mess);
		return;
	}
	rep= &re_table[port];
	if (rep->re_mode == REM_DISABLED)
	{
		/* This is the default, try to (re)locate the device. */
		rl_conf_hw(rep);
		if (rep->re_mode == REM_DISABLED)
		{
			/* Probe failed, or the device is configured off. */
			reply_mess.m_type= DL_CONF_REPLY;
			reply_mess.m_data1= -ENXIO;
			mess_reply(mp, &reply_mess);
			return;
		}
		if (rep->re_mode == REM_ENABLED)
			rl_init_hw(rep);
#if VERBOSE	/* load silently ... can always check status later */
		rl_report_link(rep);
#endif
	}

	assert(rep->re_mode == REM_ENABLED);
	assert(rep->re_flags & REF_ENABLED);

	rep->re_flags &= ~(REF_PROMISC | REF_MULTI | REF_BROAD);

	if (mp->DL_MODE & DL_PROMISC_REQ)
		rep->re_flags |= REF_PROMISC;
	if (mp->DL_MODE & DL_MULTI_REQ)
		rep->re_flags |= REF_MULTI;
	if (mp->DL_MODE & DL_BROAD_REQ)
		rep->re_flags |= REF_BROAD;

	rep->re_client = mp->m_source;
	rl_rec_mode(rep);

	reply_mess.m_type = DL_CONF_REPLY;
	reply_mess.m_data1 = mp->DL_PORT;
	reply_mess.m_data2 = RE_PORT_NR;

	reply_mess.m_data3 = (rep->re_address.ea_addr[0]|
			     (rep->re_address.ea_addr[1]<<1)|
			     (rep->re_address.ea_addr[2]<<2)|
			     (rep->re_address.ea_addr[3]<<3));
	reply_mess.m_data4 = (rep->re_address.ea_addr[4]|
			     (rep->re_address.ea_addr[5]<<1));

	mess_reply(mp, &reply_mess);
}

/*===========================================================================*
 *				rl_pci_conf				     *
 *===========================================================================*/
static void rl_pci_conf()
{
	int i, h;
	re_t *rep;
	static char envvar[] = RL_ENVVAR "#";
	static char envfmt[] = "*:d.d.d";
	static char val[128];
	long v;

	for (i= 0, rep= re_table; i<RE_PORT_NR; i++, rep++)
	{
		strcpy(rep->re_name, "rtl8139#0");
		rep->re_name[8] += i;
		rep->re_seen= FALSE;
		envvar[sizeof(RL_ENVVAR)-1]= '0'+i;
		if (0 == env_get_param(envvar, val, sizeof(val)) && 
				! env_prefix(envvar, "pci")) {
			env_panic(envvar);
		}
		v= 0;
		(void) env_parse(envvar, envfmt, 1, &v, 0, 255);
		rep->re_pcibus= v;
		v= 0;
		(void) env_parse(envvar, envfmt, 2, &v, 0, 255);
		rep->re_pcidev= v;
		v= 0;
		(void) env_parse(envvar, envfmt, 3, &v, 0, 255);
		rep->re_pcifunc= v;
	}

	pci_init();

	for (h= 1; h >= 0; h--) {
		for (i= 0, rep= re_table; i<RE_PORT_NR; i++, rep++)
		{
			if (((rep->re_pcibus | rep->re_pcidev |
				rep->re_pcifunc) != 0) != h)
			{
				continue;
			}
			if (rl_probe(rep))
				rep->re_seen= TRUE;
		}
	}
}

/*===========================================================================*
 *				rl_probe				     *
 *===========================================================================*/
static int rl_probe(rep)
re_t *rep;
{
	int i, r, devind, just_one;
	u16_t vid, did;
	u32_t bar;
	u8_t ilr;
	char *dname;

	if ((rep->re_pcibus | rep->re_pcidev | rep->re_pcifunc) != 0)
	{
		/* Look for specific PCI device */
		r= pci_find_dev(rep->re_pcibus, rep->re_pcidev,
			rep->re_pcifunc, &devind);
		if (r == 0)
		{
			printk("%s: no PCI found at %d.%d.%d\n",
				rep->re_name, rep->re_pcibus,
				rep->re_pcidev, rep->re_pcifunc);
			return 0;
		}
		pci_ids(devind, &vid, &did);
		just_one= TRUE;
	}
	else
	{
		r= pci_first_dev(&devind, &vid, &did);
		if (r == 0)
			return 0;
		just_one= FALSE;
	}

	for(;;)
	{
		for (i= 0; pcitab[i].vid != 0; i++)
		{
			if (pcitab[i].vid != vid)
				continue;
			if (pcitab[i].did != did)
				continue;
			if (pcitab[i].checkclass)
			{
			  panic("rtl_probe",
			    "class check not implemented", NO_NUM);
			}
			break;
		}
		if (pcitab[i].vid != 0)
			break;

		if (just_one)
		{
			printk(
		"%s: wrong PCI device (%04x/%04x) found at %d.%d.%d\n",
				rep->re_name, vid, did,
				rep->re_pcibus,
				rep->re_pcidev, rep->re_pcifunc);
			return 0;
		}

		r= pci_next_dev(&devind, &vid, &did);
		if (!r)
			return 0;
	}

#if VERBOSE	/* stay silent at startup, can always get status later */
	dname= pci_dev_name(vid, did);
	if (!dname)
		dname= "unknown device";
	printk("%s: ", rep->re_name);
	printk("%s (%x/%x) at %s\n", dname, vid, did, pci_slot_name(devind));
#endif
	pci_reserve(devind);
	/* printk("cr = 0x%x\n", pci_attr_r16(devind, PCI_CR)); */
	bar= pci_attr_r32(devind, PCI_BAR) & 0xffffffe0;
	if (bar < 0x400)
	{
		panic("rtl_probe",
			"base address is not properly configured", NO_NUM);
	}
	rep->re_base_port= bar;

	ilr= pci_attr_r8(devind, PCI_ILR);
	rep->re_irq= ilr;
	if (debug)
	{
		printk("%s: using I/O address 0x%lx, IRQ %d\n",
			rep->re_name, (unsigned long)bar, ilr);
	}

	return TRUE;
}

/*===========================================================================*
 *				rl_conf_hw				     *
 *===========================================================================*/
static void rl_conf_hw(rep)
re_t *rep;
{
	static eth_stat_t empty_stat = {0, 0, 0, 0, 0, 0 	/* ,... */ };

	rep->re_mode= REM_DISABLED;	/* Superfluous */

	if (rep->re_seen)
	{
		/* PCI device is present */
		rep->re_mode= REM_ENABLED;
	}
	if (rep->re_mode != REM_ENABLED)
		return;

	rep->re_flags= REF_EMPTY;
	rep->re_link_up= -1;	/* Unknown */
	rep->re_got_int= 0;
	rep->re_send_int= 0;
	rep->re_report_link= 0;
	rep->re_clear_rx= 0;
	rep->re_need_reset= 0;
	rep->re_tx_alive= 0;
	rep->re_read_s= 0;
	rep->re_tx_head= 0;
	rep->re_tx_tail= 0;
	rep->re_ertxth= RL_TSD_ERTXTH_8;
	rep->re_stat= empty_stat;
}

/*===========================================================================*
 *				rl_init_buf				     *
 *===========================================================================*/
static void rl_init_buf(rep)
re_t *rep;
{
	size_t rx_bufsize, tx_bufsize, tot_bufsize;
	phys_bytes buf;
	char *mallocbuf;
	int fd, s, i, off;

	/* Allocate receive and transmit buffers */
	tx_bufsize= ETH_MAX_PACK_SIZE_TAGGED;
	if (tx_bufsize % 4)
		tx_bufsize += 4-(tx_bufsize % 4);	/* Align */
	rx_bufsize= RX_BUFSIZE;
	tot_bufsize= N_TX_BUF*tx_bufsize + rx_bufsize;

	if (tot_bufsize % 4096)
		tot_bufsize += 4096-(tot_bufsize % 4096);

#define BUF_ALIGNMENT (64*1024)

	if(!(mallocbuf = alloc_contig(BUF_ALIGNMENT + tot_bufsize, 0, &buf))) {
	    panic("RTL8139","Couldn't allocate kernel buffer",i);
	}

	/* click-align mallocced buffer. this is what we used to get
	 * from kmalloc() too.
	 */
	if((off = buf % BUF_ALIGNMENT)) {
		mallocbuf += BUF_ALIGNMENT - off;
		buf += BUF_ALIGNMENT - off;
	}

	tell_dev((vir_bytes)mallocbuf, tot_bufsize, rep->re_pcibus, 
		rep->re_pcidev, rep->re_pcifunc);

	for (i= 0; i<N_TX_BUF; i++)
	{
		rep->re_tx[i].ret_buf= buf;
		rep->re_tx[i].v_ret_buf= mallocbuf;
		buf += tx_bufsize;
		mallocbuf += tx_bufsize;
	}
	rep->re_rx_buf= buf;
	rep->v_re_rx_buf= mallocbuf;
}

/*===========================================================================*
 *				rl_init_hw				     *
 *===========================================================================*/
static void rl_init_hw(rep)
re_t *rep;
{
	int s, i;

	rep->re_flags = REF_EMPTY;
	rep->re_flags |= REF_ENABLED;

	/* Set the interrupt handler. The policy is to only send HARD_INT 
	 * notifications. Don't reenable interrupts automatically. The id
	 * that is passed back is the interrupt line number.
	 */
	rep->re_hook_id = rep->re_irq;	
	if ((s=sys_irqsetpolicy(rep->re_irq, 0, &rep->re_hook_id)) != 0)
		printk("RTL8139: error, couldn't set IRQ policy: %d\n", s);

	rl_reset_hw(rep);

	if ((s=sys_irqenable(&rep->re_hook_id)) != 0)
		printk("RTL8139: error, couldn't enable interrupts: %d\n", s);

#if VERBOSE	/* stay silent during startup, can always get status later */
	if (rep->re_model) {
		printk("%s: model %s\n", rep->re_name, rep->re_model);
	} else
	{
		printk("%s: unknown model 0x%08x\n",
			rep->re_name,
			rl_inl(rep->re_base_port, RL_TCR) &
			(RL_TCR_HWVER_AM | RL_TCR_HWVER_BM));
	}
#endif

	rl_confaddr(rep);
	if (debug)
	{
		printk("%s: Ethernet address ", rep->re_name);
		for (i= 0; i < 6; i++)
		{
			printk("%x%c", rep->re_address.ea_addr[i],
				i < 5 ? ':' : '\n');
		}
	}
}

/*===========================================================================*
 *				rl_reset_hw				     *
 *===========================================================================*/
static void rl_reset_hw(rep)
re_t *rep;
{
	port_t port;
	u32_t t;
	phys_bytes bus_buf;
	int i;
	clock_t t0,t1;

	port= rep->re_base_port;

#if 0
	/* Reset the PHY */
	rl_outb(port, RL_BMCR, MII_CTRL_RST);
	getuptime(&t0);
	do {
		if (!(rl_inb(port, RL_BMCR) & MII_CTRL_RST))
			break;
	} while (getuptime(&t1)==0 && (t1-t0) < system_hz);
	if (rl_inb(port, RL_BMCR) & MII_CTRL_RST)
		panic("rtl8139","reset PHY failed to complete", NO_NUM);
#endif

	/* Reset the device */
	printk("rl_reset_hw: (before reset) port = 0x%x, RL_CR = 0x%x\n",
		port, rl_inb(port, RL_CR));
	rl_outb(port, RL_CR, RL_CR_RST);
	getuptime(&t0);
	do {
		if (!(rl_inb(port, RL_CR) & RL_CR_RST))
			break;
	} while (getuptime(&t1)==0 && (t1-t0) < system_hz);
	printk("rl_reset_hw: (after reset) port = 0x%x, RL_CR = 0x%x\n",
		port, rl_inb(port, RL_CR));
	if (rl_inb(port, RL_CR) & RL_CR_RST)
		printk("rtl8139: reset failed to complete");

	t= rl_inl(port, RL_TCR);
	switch(t & (RL_TCR_HWVER_AM | RL_TCR_HWVER_BM))
	{
	case RL_TCR_HWVER_RTL8139: rep->re_model= "RTL8139"; break;
	case RL_TCR_HWVER_RTL8139A: rep->re_model= "RTL8139A"; break;
	case RL_TCR_HWVER_RTL8139AG:
		rep->re_model= "RTL8139A-G / RTL8139C";
		break;
	case RL_TCR_HWVER_RTL8139B:
		rep->re_model= "RTL8139B / RTL8130";
		break;
	case RL_TCR_HWVER_RTL8100: rep->re_model= "RTL8100"; break;
	case RL_TCR_HWVER_RTL8100B:
		rep->re_model= "RTL8100B/RTL8139D";
		break;
	case RL_TCR_HWVER_RTL8139CP: rep->re_model= "RTL8139C+"; break;
	case RL_TCR_HWVER_RTL8101: rep->re_model= "RTL8101"; break;
	default:
		rep->re_model= NULL;
		break;
	}

#if 0
	printk("REVID: 0x%02x\n", rl_inb(port, RL_REVID));
#endif

	/* Intialize Rx */

	/* Should init multicast mask */
#if 0
08-0f	R/W	MAR[0-7]	multicast
#endif
	bus_buf= vm_1phys2bus(rep->re_rx_buf);
	rl_outl(port, RL_RBSTART, bus_buf);

	/* Initialize Tx */ 
	for (i= 0; i<N_TX_BUF; i++)
	{
		rep->re_tx[i].ret_busy= FALSE;
		bus_buf= vm_1phys2bus(rep->re_tx[i].ret_buf);
		rl_outl(port, RL_TSAD0+i*4, bus_buf);
		t= rl_inl(port, RL_TSD0+i*4);
		assert(t & RL_TSD_OWN);
	}

#if 0
	dump_phy(rep);
#endif

	t= rl_inw(port, RL_IMR);
	rl_outw(port, RL_IMR, t | (RL_IMR_SERR | RL_IMR_TIMEOUT |
		RL_IMR_LENCHG));

	t= rl_inw(port, RL_IMR);
	rl_outw(port, RL_IMR, t | (RL_IMR_FOVW | RL_IMR_PUN |
		RL_IMR_RXOVW | RL_IMR_RER | RL_IMR_ROK));

	t= rl_inw(port, RL_IMR);
	rl_outw(port, RL_IMR, t | (RL_IMR_TER | RL_IMR_TOK));

	t= rl_inb(port, RL_CR);
	rl_outb(port, RL_CR, t | RL_CR_RE);

	t= rl_inb(port, RL_CR);
	rl_outb(port, RL_CR, t | RL_CR_TE);

	rl_outl(port, RL_RCR, RX_BUFBITS);

	t= rl_inl(port, RL_TCR);
	rl_outl(port, RL_TCR, t | RL_TCR_IFG_STD);
}

/*===========================================================================*
 *				rl_confaddr				     *
 *===========================================================================*/
static void rl_confaddr(rep)
re_t *rep;
{
	static char eakey[]= RL_ENVVAR "#_EA";
	static char eafmt[]= "x:x:x:x:x:x";

	int i;
	port_t port;
	u32_t w;
	long v;

	/* User defined ethernet address? */
	eakey[sizeof(RL_ENVVAR)-1]= '0' + (rep-re_table);

	port= rep->re_base_port;

	for (i= 0; i < 6; i++)
	{
		if (env_parse(eakey, eafmt, i, &v, 0x00L, 0xFFL) != EP_SET)
			break;
		rep->re_address.ea_addr[i]= v;
	}

	if (i != 0 && i != 6) env_panic(eakey);	/* It's all or nothing */

	/* Should update ethernet address in hardware */
	if (i == 6)
	{
		port= rep->re_base_port;
		rl_outb(port, RL_9346CR, RL_9346CR_EEM_CONFIG);
		w= 0;
		for (i= 0; i<4; i++)
			w |= (rep->re_address.ea_addr[i] << (i*8));
		rl_outl(port, RL_IDR, w);
		w= 0;
		for (i= 4; i<6; i++)
			w |= (rep->re_address.ea_addr[i] << ((i-4)*8));
		rl_outl(port, RL_IDR+4, w);
		rl_outb(port, RL_9346CR, RL_9346CR_EEM_NORMAL);
	}

	/* Get ethernet address */
	for (i= 0; i<6; i++)
		rep->re_address.ea_addr[i]= rl_inb(port, RL_IDR+i);
}

/*===========================================================================*
 *				rl_rec_mode				     *
 *===========================================================================*/
static void rl_rec_mode(rep)
re_t *rep;
{
	port_t port;
	u32_t rcr;

	port= rep->re_base_port;
	rcr= rl_inl(port, RL_RCR);
	rcr &= ~(RL_RCR_AB|RL_RCR_AM|RL_RCR_APM|RL_RCR_AAP);
	if (rep->re_flags & REF_PROMISC)
		rcr |= RL_RCR_AB | RL_RCR_AM | RL_RCR_AAP;
	if (rep->re_flags & REF_BROAD)
		rcr |= RL_RCR_AB;
	if (rep->re_flags & REF_MULTI)
		rcr |= RL_RCR_AM;
	rcr |= RL_RCR_APM;

	rl_outl(port, RL_RCR, rcr);
}

/*===========================================================================*
 *				rl_readv				     *
 *===========================================================================*/
static void rl_readv(mp, from_int, vectored)
kipc_msg_t *mp;
int from_int;
int vectored;
{
	int i, j, n, o, s, s1, dl_port, re_client, count, size;
	port_t port;
	unsigned amount, totlen, packlen;
	phys_bytes src_phys, dst_phys, iov_src;
	u16_t d_start, d_end;
	u32_t l, rxstat = 0x12345678;
	re_t *rep;
	iovec_t *iovp;
	int cps;

	dl_port = mp->DL_PORT;
	count = mp->DL_COUNT;
	if (dl_port < 0 || dl_port >= RE_PORT_NR)
		panic("rtl8139"," illegal port", dl_port);
	rep= &re_table[dl_port];
	re_client= mp->DL_PROC;
	rep->re_client= re_client;

	if (rep->re_clear_rx)
		goto suspend;	/* Buffer overflow */

	assert(rep->re_mode == REM_ENABLED);
	assert(rep->re_flags & REF_ENABLED);

	port= rep->re_base_port;

	/* Assume that the RL_CR_BUFE check was been done by rl_checks_ints
	 */
	if (!from_int && (rl_inb(port, RL_CR) & RL_CR_BUFE))
	{
		/* Receive buffer is empty, suspend */
		goto suspend;
	}

	d_start= rl_inw(port, RL_CAPR) + RL_CAPR_DATA_OFF;
	d_end= rl_inw(port, RL_CBR) % RX_BUFSIZE;

	if (d_start >= RX_BUFSIZE)
	{
		printk("rl_readv: strange value in RL_CAPR: 0x%x\n",
			rl_inw(port, RL_CAPR));
		d_start %= RX_BUFSIZE;
	}

	if (d_end > d_start)
		amount= d_end-d_start;
	else
		amount= d_end+RX_BUFSIZE - d_start;

	rxstat = *(u32_t *) (rep->v_re_rx_buf + d_start);

	if (rep->re_clear_rx)
	{
#if 0
		printk("rl_readv: late buffer overflow\n");
#endif
		goto suspend;	/* Buffer overflow */
	}

	/* Should convert from little endian to host byte order */

	if (!(rxstat & RL_RXS_ROK))
	{
		printk("rxstat = 0x%08lx\n", rxstat);
		printk("d_start: 0x%x, d_end: 0x%x, rxstat: 0x%lx\n",
			d_start, d_end, rxstat);
		panic("rtl8139","received packet not OK", NO_NUM);
	}
	totlen= (rxstat >> RL_RXS_LEN_S);
	if (totlen < 8 || totlen > 2*ETH_MAX_PACK_SIZE)
	{
		/* Someting went wrong */
		printk(
		"rl_readv: bad length (%u) in status 0x%08lx at offset 0x%x\n",
			totlen, rxstat, d_start);
		printk(
		"d_start: 0x%x, d_end: 0x%x, totlen: %d, rxstat: 0x%lx\n",
			d_start, d_end, totlen, rxstat);
		panic(NULL, NULL, NO_NUM);
	}

#if 0
	printk("d_start: 0x%x, d_end: 0x%x, totlen: %d, rxstat: 0x%x\n",
		d_start, d_end, totlen, rxstat);
#endif

	if (totlen+4 > amount)
	{
		printk("rl_readv: packet not yet ready\n");
		goto suspend;
	}

	/* Should subtract the CRC */
	packlen= totlen - ETH_CRC_SIZE;

	if (vectored)
	{
		int iov_offset = 0;

		size= 0;
		o= d_start+4;
		src_phys= rep->re_rx_buf;
		for (i= 0; i<count; i += IOVEC_NR,
			iov_src += IOVEC_NR * sizeof(rep->re_iovec[0]),
			iov_offset += IOVEC_NR * sizeof(rep->re_iovec[0]))
		{
			n= IOVEC_NR;
			if (i+n > count)
				n= count-i;

			cps = sys_vircopy(re_client, D,
				(vir_bytes) mp->DL_ADDR + iov_offset,
				ENDPT_SELF, D, (vir_bytes) rep->re_iovec,
				n * sizeof(rep->re_iovec[0]));
			if (cps != 0)
				printk(
			"RTL8139: warning, sys_vircopy failed: %d (%d)\n",
					cps, __LINE__);

			for (j= 0, iovp= rep->re_iovec; j<n; j++, iovp++)
			{
				s= iovp->iov_size;
				if (size + s > packlen)
				{
					assert(packlen > size);
					s= packlen-size;
				}

				if (o >= RX_BUFSIZE)
				{
					o -= RX_BUFSIZE;
					assert(o < RX_BUFSIZE);
				}

				if (o+s > RX_BUFSIZE)
				{
					assert(o<RX_BUFSIZE);
					s1= RX_BUFSIZE-o;

					cps = sys_vircopy(ENDPT_SELF, D,
						(vir_bytes) rep->v_re_rx_buf+o,
						re_client, D, iovp->iov_addr,
						s1);
					if (cps != 0)
						printk(
			"RTL8139: warning, sys_vircopy failed: %d (%d)\n",
							cps, __LINE__);
					cps = sys_vircopy(ENDPT_SELF, D,
						(vir_bytes) rep->v_re_rx_buf,
						re_client, D,
						iovp->iov_addr+s1, s-s1);
					if (cps != 0)
						printk(
			"RTL8139: warning, sys_vircopy failed: %d (%d)\n",
							cps, __LINE__);
				}
				else
				{
					cps = sys_vircopy(ENDPT_SELF, D,
						(vir_bytes) rep->v_re_rx_buf+o,
						re_client, D, iovp->iov_addr,
						s);
					if (cps != 0)
						printk(
			"RTL8139: warning, sys_vircopy failed: %d (%d)\n",
							cps, __LINE__);
				}

				size += s;
				if (size == packlen)
					break;
				o += s;
			}
			if (size == packlen)
				break;
		}
		if (size < packlen)
		{
			assert(0);
		}
	}
	else
	{  
		assert(0);
#if 0
		size= mp->DL_COUNT;
		if (size < ETH_MIN_PACK_SIZE || size > ETH_MAX_PACK_SIZE_TAGGED)
			panic("rtl8139","invalid packet size", size);
		if (sys_umap(re_client, D, (vir_bytes)mp->DL_ADDR, size, &phys_user) != 0)
			panic("rtl8139","umap_local failed", NO_NUM);

		p= rep->re_tx[tx_head].ret_buf;
		cps = sys_abscopy(phys_user, p, size);
		if (cps != 0) printk("RTL8139: warning, sys_abscopy failed: %d\n", cps);
#endif
	}

	if (rep->re_clear_rx)
	{
		/* For some reason the receiver FIFO is not stopped when
		 * the buffer is full.
		 */
#if 0
		printk("rl_readv: later buffer overflow\n");
#endif
		goto suspend;	/* Buffer overflow */
	}

	rep->re_stat.ets_packetR++;
	rep->re_read_s= packlen;
	rep->re_flags= (rep->re_flags & ~REF_READING) | REF_PACK_RECV;

	/* Avoid overflow in 16-bit computations */
	l= d_start;
	l += totlen+4;
	l= (l+3) & ~3;	/* align */
	if (l >= RX_BUFSIZE)
	{
		l -= RX_BUFSIZE;
		assert(l < RX_BUFSIZE);
	}
	rl_outw(port, RL_CAPR, l-RL_CAPR_DATA_OFF);

	if (!from_int)
		reply(rep, 0, FALSE);

	return;

suspend:
	if (from_int)
	{
		assert(rep->re_flags & REF_READING);

		/* No need to store any state */
		return;
	}

	rep->re_rx_mess= *mp;
	assert(!(rep->re_flags & REF_READING));
	rep->re_flags |= REF_READING;

	reply(rep, 0, FALSE);
}

/*===========================================================================*
 *				rl_readv_s				     *
 *===========================================================================*/
static void rl_readv_s(mp, from_int)
kipc_msg_t *mp;
int from_int;
{
	int i, j, n, o, s, s1, dl_port, re_client, count, size;
	port_t port;
	unsigned amount, totlen, packlen;
	phys_bytes src_phys, dst_phys, iov_src;
	u16_t d_start, d_end;
	u32_t l, rxstat = 0x12345678;
	re_t *rep;
	iovec_s_t *iovp;
	int cps;
	int iov_offset = 0;

	dl_port = mp->DL_PORT;
	count = mp->DL_COUNT;
	if (dl_port < 0 || dl_port >= RE_PORT_NR)
		panic("rtl8139"," illegal port", dl_port);
	rep= &re_table[dl_port];
	re_client= mp->DL_PROC;
	rep->re_client= re_client;

	if (rep->re_clear_rx)
		goto suspend;	/* Buffer overflow */

	assert(rep->re_mode == REM_ENABLED);
	assert(rep->re_flags & REF_ENABLED);

	port= rep->re_base_port;

	/* Assume that the RL_CR_BUFE check was been done by rl_checks_ints
	 */
	if (!from_int && (rl_inb(port, RL_CR) & RL_CR_BUFE))
	{
		/* Receive buffer is empty, suspend */
		goto suspend;
	}

	d_start= rl_inw(port, RL_CAPR) + RL_CAPR_DATA_OFF;
	d_end= rl_inw(port, RL_CBR) % RX_BUFSIZE;

	if (d_start >= RX_BUFSIZE)
	{
		printk("rl_readv: strange value in RL_CAPR: 0x%x\n",
			rl_inw(port, RL_CAPR));
		d_start %= RX_BUFSIZE;
	}

	if (d_end > d_start)
		amount= d_end-d_start;
	else
		amount= d_end+RX_BUFSIZE - d_start;

	rxstat = *(u32_t *) (rep->v_re_rx_buf + d_start);

	if (rep->re_clear_rx)
	{
#if 0
		printk("rl_readv: late buffer overflow\n");
#endif
		goto suspend;	/* Buffer overflow */
	}

	/* Should convert from little endian to host byte order */

	if (!(rxstat & RL_RXS_ROK))
	{
		printk("rxstat = 0x%08lx\n", rxstat);
		printk("d_start: 0x%x, d_end: 0x%x, rxstat: 0x%lx\n",
			d_start, d_end, rxstat);
		panic("rtl8139","received packet not OK", NO_NUM);
	}
	totlen= (rxstat >> RL_RXS_LEN_S);
	if (totlen < 8 || totlen > 2*ETH_MAX_PACK_SIZE)
	{
		/* Someting went wrong */
		printk(
		"rl_readv: bad length (%u) in status 0x%08lx at offset 0x%x\n",
			totlen, rxstat, d_start);
		printk(
		"d_start: 0x%x, d_end: 0x%x, totlen: %d, rxstat: 0x%lx\n",
			d_start, d_end, totlen, rxstat);
		panic(NULL, NULL, NO_NUM);
	}

#if 0
	printk("d_start: 0x%x, d_end: 0x%x, totlen: %d, rxstat: 0x%x\n",
		d_start, d_end, totlen, rxstat);
#endif

	if (totlen+4 > amount)
	{
		printk("rl_readv: packet not yet ready\n");
		goto suspend;
	}

	/* Should subtract the CRC */
	packlen= totlen - ETH_CRC_SIZE;

	size= 0;
	o= d_start+4;
	src_phys= rep->re_rx_buf;
	for (i= 0; i<count; i += IOVEC_NR,
		iov_src += IOVEC_NR * sizeof(rep->re_iovec_s[0]),
		iov_offset += IOVEC_NR * sizeof(rep->re_iovec_s[0]))
	{
		n= IOVEC_NR;
		if (i+n > count)
			n= count-i;

		cps = sys_safecopyfrom(re_client, mp->DL_GRANT, iov_offset,
			(vir_bytes) rep->re_iovec_s,
			n * sizeof(rep->re_iovec_s[0]), D);
		if (cps != 0)
		{
			panic(__FILE__, "rl_readv_s: sys_safecopyfrom failed",
				cps);
		}

		for (j= 0, iovp= rep->re_iovec_s; j<n; j++, iovp++)
		{
			s= iovp->iov_size;
			if (size + s > packlen)
			{
				assert(packlen > size);
				s= packlen-size;
			}

#if 0
			if (sys_umap(re_client, D, iovp->iov_addr, s, &dst_phys) != 0)
			  panic("rtl8139","umap_local failed\n", NO_NUM);
#endif

			if (o >= RX_BUFSIZE)
			{
				o -= RX_BUFSIZE;
				assert(o < RX_BUFSIZE);
			}

			if (o+s > RX_BUFSIZE)
			{
				assert(o<RX_BUFSIZE);
				s1= RX_BUFSIZE-o;

				cps = sys_safecopyto(re_client,
					iovp->iov_grant, 0, 
					(vir_bytes) rep->v_re_rx_buf+o, s1, D);
				if (cps != 0)
				{
					panic(__FILE__,
					"rl_readv_s: sys_safecopyto failed",
						cps);
				}
				cps = sys_safecopyto(re_client,
					iovp->iov_grant, s1, 
					(vir_bytes) rep->v_re_rx_buf, s-s1, S);
				if (cps != 0)
				{
					panic(__FILE__,
					"rl_readv_s: sys_safecopyto failed",
						cps);
				}
			}
			else
			{
				cps = sys_safecopyto(re_client,
					iovp->iov_grant, 0,
					(vir_bytes) rep->v_re_rx_buf+o, s, D);
				if (cps != 0)
					panic(__FILE__,
					"rl_readv_s: sys_safecopyto failed",
						cps);
			}

			size += s;
			if (size == packlen)
				break;
			o += s;
		}
		if (size == packlen)
			break;
	}
	if (size < packlen)
	{
		assert(0);
	}

	if (rep->re_clear_rx)
	{
		/* For some reason the receiver FIFO is not stopped when
		 * the buffer is full.
		 */
#if 0
		printk("rl_readv: later buffer overflow\n");
#endif
		goto suspend;	/* Buffer overflow */
	}

	rep->re_stat.ets_packetR++;
	rep->re_read_s= packlen;
	rep->re_flags= (rep->re_flags & ~REF_READING) | REF_PACK_RECV;

	/* Avoid overflow in 16-bit computations */
	l= d_start;
	l += totlen+4;
	l= (l+3) & ~3;	/* align */
	if (l >= RX_BUFSIZE)
	{
		l -= RX_BUFSIZE;
		assert(l < RX_BUFSIZE);
	}
	rl_outw(port, RL_CAPR, l-RL_CAPR_DATA_OFF);

	if (!from_int)
		reply(rep, 0, FALSE);

	return;

suspend:
	if (from_int)
	{
		assert(rep->re_flags & REF_READING);

		/* No need to store any state */
		return;
	}

	rep->re_rx_mess= *mp;
	assert(!(rep->re_flags & REF_READING));
	rep->re_flags |= REF_READING;

	reply(rep, 0, FALSE);
}

/*===========================================================================*
 *				rl_writev				     *
 *===========================================================================*/
static void rl_writev(mp, from_int, vectored)
kipc_msg_t *mp;
int from_int;
int vectored;
{
	phys_bytes iov_src, phys_user;
	int i, j, n, s, port, count, size;
	int tx_head, re_client;
	re_t *rep;
	iovec_t *iovp;
	char *ret;
	int cps;

	port = mp->DL_PORT;
	count = mp->DL_COUNT;
	if (port < 0 || port >= RE_PORT_NR)
		panic("rtl8139","illegal port", port);
	rep= &re_table[port];
	re_client= mp->DL_PROC;
	rep->re_client= re_client;

	assert(rep->re_mode == REM_ENABLED);
	assert(rep->re_flags & REF_ENABLED);

	if (from_int)
	{
		assert(rep->re_flags & REF_SEND_AVAIL);
		rep->re_flags &= ~REF_SEND_AVAIL;
		rep->re_send_int= FALSE;
		rep->re_tx_alive= TRUE;
	}

	tx_head= rep->re_tx_head;
	if (rep->re_tx[tx_head].ret_busy)
	{
		assert(!(rep->re_flags & REF_SEND_AVAIL));
		rep->re_flags |= REF_SEND_AVAIL;
		if (rep->re_tx[tx_head].ret_busy)
			goto suspend;

		/* Race condition, the interrupt handler may clear re_busy
		 * before we got a chance to set REF_SEND_AVAIL. Checking
		 * ret_busy twice should be sufficient.
		 */
#if 0
		printk("rl_writev: race detected\n");
#endif
		rep->re_flags &= ~REF_SEND_AVAIL;
		rep->re_send_int= FALSE;
	}

	assert(!(rep->re_flags & REF_SEND_AVAIL));
	assert(!(rep->re_flags & REF_PACK_SENT));

	if (vectored)
	{
		int iov_offset = 0;

		size= 0;
		ret = rep->re_tx[tx_head].v_ret_buf;
		for (i= 0; i<count; i += IOVEC_NR,
			iov_src += IOVEC_NR * sizeof(rep->re_iovec[0]),
			iov_offset += IOVEC_NR * sizeof(rep->re_iovec[0]))
		{
			n= IOVEC_NR;
			if (i+n > count)
				n= count-i;
			cps = sys_vircopy(re_client, D, ((vir_bytes) mp->DL_ADDR) + iov_offset,
				ENDPT_SELF, D, (vir_bytes) rep->re_iovec, 
				n * sizeof(rep->re_iovec[0]));
if (cps != 0) printk("RTL8139: warning, sys_vircopy failed: %d\n", cps);

			for (j= 0, iovp= rep->re_iovec; j<n; j++, iovp++)
			{
				s= iovp->iov_size;
				if (size + s > ETH_MAX_PACK_SIZE_TAGGED)
				{
				  panic("rtl8139","invalid packet size",
			  	    NO_NUM);
				}

				if (sys_umap(re_client, D, iovp->iov_addr, s, &phys_user) != 0)
				  panic("rtl8139","umap_local failed\n", NO_NUM);

				cps = sys_vircopy(re_client, D, iovp->iov_addr,
					ENDPT_SELF, D, (vir_bytes) ret, s);
		if (cps != 0) printk("RTL8139: warning, sys_vircopy failed: %d\n", cps);
				size += s;
				ret += s;
			}
		}
		if (size < ETH_MIN_PACK_SIZE)
			panic("rtl8139","invalid packet size", size);
	}
	else
	{  
		size= mp->DL_COUNT;
		if (size < ETH_MIN_PACK_SIZE || size > ETH_MAX_PACK_SIZE_TAGGED)
			panic("rtl8139","invalid packet size", size);
		ret = rep->re_tx[tx_head].v_ret_buf;
		cps = sys_vircopy(re_client, D, (vir_bytes)mp->DL_ADDR, 
			ENDPT_SELF, D, (vir_bytes) ret, size);
	if (cps != 0) printk("RTL8139: warning, sys_abscopy failed: %d\n", cps);
	}

	rl_outl(rep->re_base_port, RL_TSD0+tx_head*4, 
		rep->re_ertxth | size);
	rep->re_tx[tx_head].ret_busy= TRUE;

	if (++tx_head == N_TX_BUF)
		tx_head= 0;
	assert(tx_head < RL_N_TX);
	rep->re_tx_head= tx_head;

	rep->re_flags |= REF_PACK_SENT;

	/* If the interrupt handler called, don't send a reply. The reply
	 * will be sent after all interrupts are handled. 
	 */
	if (from_int)
		return;
	reply(rep, 0, FALSE);
	return;

suspend:
#if 0
		printk("rl_writev: head %d, tail %d, busy: %d %d %d %d\n",
			tx_head, rep->re_tx_tail,
			rep->re_tx[0].ret_busy, rep->re_tx[1].ret_busy,
			rep->re_tx[2].ret_busy, rep->re_tx[3].ret_busy);
		printk("rl_writev: TSD: 0x%x, 0x%x, 0x%x, 0x%x\n",
			rl_inl(rep->re_base_port, RL_TSD0+0*4),
			rl_inl(rep->re_base_port, RL_TSD0+1*4),
			rl_inl(rep->re_base_port, RL_TSD0+2*4),
			rl_inl(rep->re_base_port, RL_TSD0+3*4));
#endif

	if (from_int)
		panic("rtl8139","should not be sending\n", NO_NUM);

	rep->re_tx_mess= *mp;
	reply(rep, 0, FALSE);
}

/*===========================================================================*
 *				rl_writev_s				     *
 *===========================================================================*/
static void rl_writev_s(mp, from_int)
kipc_msg_t *mp;
int from_int;
{
	phys_bytes iov_src;
	int i, j, n, s, port, count, size;
	int tx_head, re_client;
	re_t *rep;
	iovec_s_t *iovp;
	char *ret;
	int cps;
	int iov_offset = 0;

	port = mp->DL_PORT;
	count = mp->DL_COUNT;
	if (port < 0 || port >= RE_PORT_NR)
		panic("rtl8139","illegal port", port);
	rep= &re_table[port];
	re_client= mp->DL_PROC;
	rep->re_client= re_client;

	assert(rep->re_mode == REM_ENABLED);
	assert(rep->re_flags & REF_ENABLED);

	if (from_int)
	{
		assert(rep->re_flags & REF_SEND_AVAIL);
		rep->re_flags &= ~REF_SEND_AVAIL;
		rep->re_send_int= FALSE;
		rep->re_tx_alive= TRUE;
	}

	tx_head= rep->re_tx_head;
	if (rep->re_tx[tx_head].ret_busy)
	{
		assert(!(rep->re_flags & REF_SEND_AVAIL));
		rep->re_flags |= REF_SEND_AVAIL;
		if (rep->re_tx[tx_head].ret_busy)
			goto suspend;

		/* Race condition, the interrupt handler may clear re_busy
		 * before we got a chance to set REF_SEND_AVAIL. Checking
		 * ret_busy twice should be sufficient.
		 */
#if 0
		printk("rl_writev: race detected\n");
#endif
		rep->re_flags &= ~REF_SEND_AVAIL;
		rep->re_send_int= FALSE;
	}

	assert(!(rep->re_flags & REF_SEND_AVAIL));
	assert(!(rep->re_flags & REF_PACK_SENT));

	size= 0;
	ret = rep->re_tx[tx_head].v_ret_buf;
	for (i= 0; i<count; i += IOVEC_NR,
		iov_src += IOVEC_NR * sizeof(rep->re_iovec_s[0]),
		iov_offset += IOVEC_NR * sizeof(rep->re_iovec_s[0]))
	{
		n= IOVEC_NR;
		if (i+n > count)
			n= count-i;
		cps = sys_safecopyfrom(re_client, mp->DL_GRANT, iov_offset,
			(vir_bytes) rep->re_iovec_s,
			n * sizeof(rep->re_iovec_s[0]), D);
		if (cps != 0)
		{
			panic(__FILE__, "rl_writev_s: sys_safecopyfrom failed",
				cps);
		}

		for (j= 0, iovp= rep->re_iovec_s; j<n; j++, iovp++)
		{
			s= iovp->iov_size;
			if (size + s > ETH_MAX_PACK_SIZE_TAGGED)
			{
			  panic("rtl8139","invalid packet size",
			    NO_NUM);
			}
			cps = sys_safecopyfrom(re_client, iovp->iov_grant, 0,
				(vir_bytes) ret, s, D);
			if (cps != 0)
			{
				panic(__FILE__,
					"rl_writev_s: sys_safecopyfrom failed",
					cps);
			}
			size += s;
			ret += s;
		}
	}
	if (size < ETH_MIN_PACK_SIZE)
		panic("rtl8139","invalid packet size", size);

	rl_outl(rep->re_base_port, RL_TSD0+tx_head*4, 
		rep->re_ertxth | size);
	rep->re_tx[tx_head].ret_busy= TRUE;

	if (++tx_head == N_TX_BUF)
		tx_head= 0;
	assert(tx_head < RL_N_TX);
	rep->re_tx_head= tx_head;

	rep->re_flags |= REF_PACK_SENT;

	/* If the interrupt handler called, don't send a reply. The reply
	 * will be sent after all interrupts are handled. 
	 */
	if (from_int)
		return;
	reply(rep, 0, FALSE);
	return;

suspend:
#if 0
		printk("rl_writev: head %d, tail %d, busy: %d %d %d %d\n",
			tx_head, rep->re_tx_tail,
			rep->re_tx[0].ret_busy, rep->re_tx[1].ret_busy,
			rep->re_tx[2].ret_busy, rep->re_tx[3].ret_busy);
		printk("rl_writev: TSD: 0x%x, 0x%x, 0x%x, 0x%x\n",
			rl_inl(rep->re_base_port, RL_TSD0+0*4),
			rl_inl(rep->re_base_port, RL_TSD0+1*4),
			rl_inl(rep->re_base_port, RL_TSD0+2*4),
			rl_inl(rep->re_base_port, RL_TSD0+3*4));
#endif

	if (from_int)
		panic("rtl8139","should not be sending\n", NO_NUM);

	rep->re_tx_mess= *mp;
	reply(rep, 0, FALSE);
}

/*===========================================================================*
 *				rl_check_ints				     *
 *===========================================================================*/
static void rl_check_ints(rep)
re_t *rep;
{
#if 0
10-1f	R/W	TSD[0-3]	Transmit Status of Descriptor [0-3]
	31	R	CRS	Carrier Sense Lost
	30	R	TABT	Transmit Abort
	29	R	OWC	Out of Window Collision
	27-24	R	NCC[3-0] Number of Collision Count
	23-22			reserved
	21-16	R/W	ERTXH[5-0] Early Tx Threshold
	15	R	TOK	Transmit OK
	14	R	TUN	Transmit FIFO Underrun
	13	R/W	OWN	OWN
	12-0	R/W	SIZE	Descriptor Size
3e-3f	R/W	ISR		Interrupt Status Register
	6	R/W	FOVW	Fx FIFO Overflow Interrupt
	5	R/W	PUN/LinkChg Packet Underrun / Link Change Interrupt
	3	R/W	TER	Transmit Error Interrupt
	2	R/W	TOK	Transmit OK Interrupt
3e-3f	R/W	ISR		Interrupt Status Register
	15	R/W	SERR	System Error Interrupt
	14	R/W	TimeOut	Time Out Interrupt
	13	R/W	LenChg	Cable Length Change Interrupt
3e-3f	R/W	ISR		Interrupt Status Register
	4	R/W	RXOVW	Rx Buffer Overflow Interrupt
	1	R/W	RER	Receive Error Interrupt
	0	R/W	ROK	Receive OK Interrupt
4c-4f	R/W	MPC		Missed Packet Counter
60-61	R	TSAD		Transmit Status of All Descriptors
	15-12	R	TOK[3-0] TOK bit of Descriptor [3-0]
	11-8	R	TUN[3-0] TUN bit of Descriptor [3-0]
	7-4	R	TABT[3-0] TABT bit of Descriptor [3-0]
	3-0     R       OWN[3-0] OWN bit of Descriptor [3-0]
6c-6d	R	DIS		Disconnect Counter
	15-0	R	DCNT	Disconnect Counter
6e-6f	R	FCSC		False Carrier Sense Counter
	15-0	R	FCSCNT	False Carrier event counter
72-73	R	REC		RX_ER Counter
	15-0	R	RXERCNT	Received packet counter
#endif

	int re_flags;

	re_flags= rep->re_flags;

	if ((re_flags & REF_READING) &&
		!(rl_inb(rep->re_base_port, RL_CR) & RL_CR_BUFE))
	{
		if (rep->re_rx_mess.m_type == DL_READV)
		{
			rl_readv(&rep->re_rx_mess, TRUE /* from int */,
				TRUE /* vectored */);
		}
		else if (rep->re_rx_mess.m_type == DL_READV_S)
		{
			rl_readv_s(&rep->re_rx_mess, TRUE /* from int */);
		}
		else
		{
			assert(rep->re_rx_mess.m_type == DL_READ);
			rl_readv(&rep->re_rx_mess, TRUE /* from int */,
				FALSE /* !vectored */);
		}
	}
	if (rep->re_clear_rx)
		rl_clear_rx(rep);

	if (rep->re_need_reset)
		rl_do_reset(rep);

	if (rep->re_send_int)
	{
		if (rep->re_tx_mess.m_type == DL_WRITEV)
		{
			rl_writev(&rep->re_tx_mess, TRUE /* from int */,
				TRUE /* vectored */);
		}
		else if (rep->re_tx_mess.m_type == DL_WRITEV_S)
		{
			rl_writev_s(&rep->re_tx_mess, TRUE /* from int */);
		}
		else
		{
			assert(rep->re_tx_mess.m_type == DL_WRITE);
			rl_writev(&rep->re_tx_mess, TRUE /* from int */,
				FALSE /* !vectored */);
		}
	}

	if (rep->re_report_link)
		rl_report_link(rep);

	if (rep->re_flags & (REF_PACK_SENT | REF_PACK_RECV))
		reply(rep, 0, TRUE);
}

/*===========================================================================*
 *				rl_report_link				     *
 *===========================================================================*/
static void rl_report_link(rep)
re_t *rep;
{
	port_t port;
	u16_t mii_ctrl, mii_status, mii_ana, mii_anlpa, mii_ane, mii_extstat;
	u8_t msr;
	int f, link_up;

	rep->re_report_link= FALSE;
	port= rep->re_base_port;
	msr= rl_inb(port, RL_MSR);
	link_up= !(msr & RL_MSR_LINKB);
	rep->re_link_up= link_up;
	if (!link_up)
	{
		printk("%s: link down\n", rep->re_name);
		return;
	}

	mii_ctrl= rl_inw(port, RL_BMCR);
	mii_status= rl_inw(port, RL_BMSR);
	mii_ana= rl_inw(port, RL_ANAR);
	mii_anlpa= rl_inw(port, RL_ANLPAR);
	mii_ane= rl_inw(port, RL_ANER);
	mii_extstat= 0;

	if (mii_ctrl & (MII_CTRL_LB|MII_CTRL_PD|MII_CTRL_ISO))
	{
		printk("%s: PHY: ", rep->re_name);
		f= 1;
		if (mii_ctrl & MII_CTRL_LB)
		{
			printk("loopback mode");
			f= 0;
		}
		if (mii_ctrl & MII_CTRL_PD)
		{
			if (!f) printk(", ");
			f= 0;
			printk("powered down");
		}
		if (mii_ctrl & MII_CTRL_ISO)
		{
			if (!f) printk(", ");
			f= 0;
			printk("isolated");
		}
		printk("\n");
		return;
	}
	if (!(mii_ctrl & MII_CTRL_ANE))
	{
		printk("%s: manual config: ", rep->re_name);
		switch(mii_ctrl & (MII_CTRL_SP_LSB|MII_CTRL_SP_MSB))
		{
		case MII_CTRL_SP_10:	printk("10 Mbps"); break;
		case MII_CTRL_SP_100:	printk("100 Mbps"); break;
		case MII_CTRL_SP_1000:	printk("1000 Mbps"); break;
		case MII_CTRL_SP_RES:	printk("reserved speed"); break;
		}
		if (mii_ctrl & MII_CTRL_DM)
			printk(", full duplex");
		else
			printk(", half duplex");
		printk("\n");
		return;
	}

	if (!debug) goto resspeed;

	printk("%s: ", rep->re_name);
	mii_print_stat_speed(mii_status, mii_extstat);
	printk("\n");

	if (!(mii_status & MII_STATUS_ANC))
		printk("%s: auto-negotiation not complete\n", rep->re_name);
	if (mii_status & MII_STATUS_RF)
		printk("%s: remote fault detected\n", rep->re_name);
	if (!(mii_status & MII_STATUS_ANA))
	{
		printk("%s: local PHY has no auto-negotiation ability\n",
			rep->re_name);
	}
	if (!(mii_status & MII_STATUS_LS))
		printk("%s: link down\n", rep->re_name);
	if (mii_status & MII_STATUS_JD)
		printk("%s: jabber condition detected\n", rep->re_name);
	if (!(mii_status & MII_STATUS_EC))
	{
		printk("%s: no extended register set\n", rep->re_name);
		goto resspeed;
	}
	if (!(mii_status & MII_STATUS_ANC))
		goto resspeed;

	printk("%s: local cap.: ", rep->re_name);
	mii_print_techab(mii_ana);
	printk("\n");

	if (mii_ane & MII_ANE_PDF)
		printk("%s: parallel detection fault\n", rep->re_name);
	if (!(mii_ane & MII_ANE_LPANA))
	{
		printk("%s: link-partner does not support auto-negotiation\n",
			rep->re_name);
		goto resspeed;
	}

	printk("%s: remote cap.: ", rep->re_name);
	mii_print_techab(mii_anlpa);
	printk("\n");

resspeed:
	printk("%s: ", rep->re_name);
	printk("link up at %d Mbps, ", (msr & RL_MSR_SPEED_10) ? 10 : 100);
	printk("%s duplex\n", ((mii_ctrl & MII_CTRL_DM) ? "full" : "half"));

}

static void mii_print_techab(techab)
u16_t techab;
{
	int fs, ft;
	if ((techab & MII_ANA_SEL_M) != MII_ANA_SEL_802_3)
	{
		printk("strange selector 0x%x, value 0x%x",
			techab & MII_ANA_SEL_M,
			(techab & MII_ANA_TAF_M) >> MII_ANA_TAF_S);
		return;
	}
	fs= 1;
	if (techab & (MII_ANA_100T4 | MII_ANA_100TXFD | MII_ANA_100TXHD))
	{
		printk("100 Mbps: ");
		fs= 0;
		ft= 1;
		if (techab & MII_ANA_100T4)
		{
			printk("T4");
			ft= 0;
		}
		if (techab & (MII_ANA_100TXFD | MII_ANA_100TXHD))
		{
			if (!ft)
				printk(", ");
			ft= 0;
			printk("TX-");
			switch(techab & (MII_ANA_100TXFD|MII_ANA_100TXHD))
			{
			case MII_ANA_100TXFD:	printk("FD"); break;
			case MII_ANA_100TXHD:	printk("HD"); break;
			default:		printk("FD/HD"); break;
			}
		}
	}
	if (techab & (MII_ANA_10TFD | MII_ANA_10THD))
	{
		if (!fs)
			printk(", ");
		printk("10 Mbps: ");
		fs= 0;
		printk("T-");
		switch(techab & (MII_ANA_10TFD|MII_ANA_10THD))
		{
		case MII_ANA_10TFD:	printk("FD"); break;
		case MII_ANA_10THD:	printk("HD"); break;
		default:		printk("FD/HD"); break;
		}
	}
	if (techab & MII_ANA_PAUSE_SYM)
	{
		if (!fs)
			printk(", ");
		fs= 0;
		printk("pause(SYM)");
	}
	if (techab & MII_ANA_PAUSE_ASYM)
	{
		if (!fs)
			printk(", ");
		fs= 0;
		printk("pause(ASYM)");
	}
	if (techab & MII_ANA_TAF_RES)
	{
		if (!fs)
			printk(", ");
		fs= 0;
		printk("0x%x", (techab & MII_ANA_TAF_RES) >> MII_ANA_TAF_S);
	}
}

static void mii_print_stat_speed(stat, extstat)
u16_t stat;
u16_t extstat;
{
	int fs, ft;
	fs= 1;
	if (stat & MII_STATUS_EXT_STAT)
	{
		if (extstat & (MII_ESTAT_1000XFD | MII_ESTAT_1000XHD |
			MII_ESTAT_1000TFD | MII_ESTAT_1000THD))
		{
			printk("1000 Mbps: ");
			fs= 0;
			ft= 1;
			if (extstat & (MII_ESTAT_1000XFD | MII_ESTAT_1000XHD))
			{
				ft= 0;
				printk("X-");
				switch(extstat &
					(MII_ESTAT_1000XFD|MII_ESTAT_1000XHD))
				{
				case MII_ESTAT_1000XFD:	printk("FD"); break;
				case MII_ESTAT_1000XHD:	printk("HD"); break;
				default:		printk("FD/HD"); break;
				}
			}
			if (extstat & (MII_ESTAT_1000TFD | MII_ESTAT_1000THD))
			{
				if (!ft)
					printk(", ");
				ft= 0;
				printk("T-");
				switch(extstat &
					(MII_ESTAT_1000TFD|MII_ESTAT_1000THD))
				{
				case MII_ESTAT_1000TFD:	printk("FD"); break;
				case MII_ESTAT_1000THD:	printk("HD"); break;
				default:		printk("FD/HD"); break;
				}
			}
		}
	}
	if (stat & (MII_STATUS_100T4 |
		MII_STATUS_100XFD | MII_STATUS_100XHD |
		MII_STATUS_100T2FD | MII_STATUS_100T2HD))
	{
		if (!fs)
			printk(", ");
		fs= 0;
		printk("100 Mbps: ");
		ft= 1;
		if (stat & MII_STATUS_100T4)
		{
			printk("T4");
			ft= 0;
		}
		if (stat & (MII_STATUS_100XFD | MII_STATUS_100XHD))
		{
			if (!ft)
				printk(", ");
			ft= 0;
			printk("TX-");
			switch(stat & (MII_STATUS_100XFD|MII_STATUS_100XHD))
			{
			case MII_STATUS_100XFD:	printk("FD"); break;
			case MII_STATUS_100XHD:	printk("HD"); break;
			default:		printk("FD/HD"); break;
			}
		}
		if (stat & (MII_STATUS_100T2FD | MII_STATUS_100T2HD))
		{
			if (!ft)
				printk(", ");
			ft= 0;
			printk("T2-");
			switch(stat & (MII_STATUS_100T2FD|MII_STATUS_100T2HD))
			{
			case MII_STATUS_100T2FD:	printk("FD"); break;
			case MII_STATUS_100T2HD:	printk("HD"); break;
			default:		printk("FD/HD"); break;
			}
		}
	}
	if (stat & (MII_STATUS_10FD | MII_STATUS_10HD))
	{
		if (!fs)
			printk(", ");
		printk("10 Mbps: ");
		fs= 0;
		printk("T-");
		switch(stat & (MII_STATUS_10FD|MII_STATUS_10HD))
		{
		case MII_STATUS_10FD:	printk("FD"); break;
		case MII_STATUS_10HD:	printk("HD"); break;
		default:		printk("FD/HD"); break;
		}
	}
}

/*===========================================================================*
 *				rl_clear_rx				     *
 *===========================================================================*/
static void rl_clear_rx(rep)
re_t *rep;
{
	port_t port;
	u8_t cr;
	int i;
	clock_t t0,t1;

	rep->re_clear_rx= FALSE;
	port= rep->re_base_port;

	/* Reset the receiver */
	cr= rl_inb(port, RL_CR);
	cr &= ~RL_CR_RE;
	rl_outb(port, RL_CR, cr);
	getuptime(&t0);
	do {
		if (!(rl_inb(port, RL_CR) & RL_CR_RE))
			break;
	} while (getuptime(&t1)==0 && (t1-t0) < system_hz);
	if (rl_inb(port, RL_CR) & RL_CR_RE)
		panic("rtl8139","cannot disable receiver", NO_NUM);

#if 0
	printk("RBSTART = 0x%08x\n", rl_inl(port, RL_RBSTART));
	printk("CAPR = 0x%04x\n", rl_inw(port, RL_CAPR));
	printk("CBR = 0x%04x\n", rl_inw(port, RL_CBR));
	printk("RCR = 0x%08x\n", rl_inl(port, RL_RCR));
#endif

	rl_outb(port, RL_CR, cr | RL_CR_RE);

	rl_outl(port, RL_RCR, RX_BUFBITS);

	rl_rec_mode(rep);

	rep->re_stat.ets_missedP++;
}

/*===========================================================================*
 *				rl_do_reset				     *
 *===========================================================================*/
static void rl_do_reset(rep)
re_t *rep;
{
	rep->re_need_reset= FALSE;
	rl_reset_hw(rep);
	rl_rec_mode(rep);

	rep->re_tx_head= 0;
	if (rep->re_flags & REF_SEND_AVAIL)
	{
		rep->re_tx[rep->re_tx_head].ret_busy= FALSE;
		rep->re_send_int= TRUE;
	}
}

/*===========================================================================*
 *				rl_getstat				     *
 *===========================================================================*/
static void rl_getstat(mp)
kipc_msg_t *mp;
{
	int r, port;
	eth_stat_t stats;
	re_t *rep;

	port = mp->DL_PORT;
	if (port < 0 || port >= RE_PORT_NR)
		panic("rtl8139","illegal port", port);
	rep= &re_table[port];
	rep->re_client= mp->DL_PROC;

	assert(rep->re_mode == REM_ENABLED);
	assert(rep->re_flags & REF_ENABLED);

	stats= rep->re_stat;

	r = sys_datacopy(ENDPT_SELF, (vir_bytes) &stats, mp->DL_PROC,
		(vir_bytes) mp->DL_ADDR, sizeof(stats));
	if (r != 0)
		panic(__FILE__, "rl_getstat: sys_datacopy failed", r);

	mp->m_type= DL_STAT_REPLY;
	mp->DL_PORT= port;
	mp->DL_STAT= 0;
	r= kipc_module_call(KIPC_SEND, 0, mp->m_source, mp);
	if (r != 0)
		panic("RTL8139", "rl_getstat: send failed: %d\n", r);
}

/*===========================================================================*
 *				rl_getstat_s				     *
 *===========================================================================*/
static void rl_getstat_s(mp)
kipc_msg_t *mp;
{
	int r, port;
	eth_stat_t stats;
	re_t *rep;

	port = mp->DL_PORT;
	if (port < 0 || port >= RE_PORT_NR)
		panic("rtl8139","illegal port", port);
	rep= &re_table[port];
	rep->re_client= mp->DL_PROC;

	assert(rep->re_mode == REM_ENABLED);
	assert(rep->re_flags & REF_ENABLED);

	stats= rep->re_stat;

	r = sys_safecopyto(mp->DL_PROC, mp->DL_GRANT, 0,
		(vir_bytes) &stats, sizeof(stats), D);
	if (r != 0)
		panic(__FILE__, "rl_getstat_s: sys_safecopyto failed", r);

	mp->m_type= DL_STAT_REPLY;
	mp->DL_PORT= port;
	mp->DL_STAT= 0;
	r= kipc_module_call(KIPC_SEND, 0, mp->m_source, mp);
	if (r != 0)
		panic("RTL8139", "rl_getstat_s: send failed: %d\n", r);
}


/*===========================================================================*
 *				rl_getname				     *
 *===========================================================================*/
static void rl_getname(mp)
kipc_msg_t *mp;
{
	int r;

	mp->DL_NAME = progname;
	mp->m_type= DL_NAME_REPLY;
	r= kipc_module_call(KIPC_SEND, 0, mp->m_source, mp);
	if (r != 0)
		panic("RTL8139", "rl_getname: send failed: %d\n", r);
}


/*===========================================================================*
 *				reply					     *
 *===========================================================================*/
static void reply(rep, err, may_block)
re_t *rep;
int err;
int may_block;
{
	kipc_msg_t reply;
	int status;
	int r;
	clock_t now;

	status = 0;
	if (rep->re_flags & REF_PACK_SENT)
		status |= DL_PACK_SEND;
	if (rep->re_flags & REF_PACK_RECV)
		status |= DL_PACK_RECV;

	reply.m_type = DL_TASK_REPLY;
	reply.DL_PORT = rep - re_table;
	reply.DL_PROC = rep->re_client;
	reply.DL_STAT = status | ((u32_t) err << 16);
	reply.DL_COUNT = rep->re_read_s;
	if ((r = getuptime(&now)) != 0)
		panic("rtl8139","getuptime() failed:", r);
	reply.DL_CLCK = now;

	r= kipc_module_call(KIPC_SEND, 0, rep->re_client, &reply);

	if (r == -ELOCKED && may_block)
	{
#if 0
		printW(); printk("send locked\n");
#endif
		return;
	}

	if (r < 0) {
		printk("RTL8139 tried sending to %d, type %d\n", rep->re_client, reply.m_type);
		panic("rtl8139","send failed:", r);
	}
	
	rep->re_read_s = 0;
	rep->re_flags &= ~(REF_PACK_SENT | REF_PACK_RECV);
}

/*===========================================================================*
 *				mess_reply				     *
 *===========================================================================*/
static void mess_reply(req, reply_mess)
kipc_msg_t *req;
kipc_msg_t *reply_mess;
{
	if (kipc_module_call(KIPC_SEND, 0, req->m_source, reply_mess) != 0)
		panic("rtl8139","unable to mess_reply", NO_NUM);
}

#if 0
static void dump_phy(rep)
re_t *rep;
{
	port_t port;
	u32_t t;

	port= rep->re_base_port;

	t= rl_inb(port, RL_MSR);
	printk("MSR: 0x%02lx\n", t);
	if (t & RL_MSR_SPEED_10)
		printk("\t10 Mbps\n");
	if (t & RL_MSR_LINKB)
		printk("\tLink failed\n");

	t= rl_inb(port, RL_CONFIG1);
	printk("CONFIG1: 0x%02lx\n", t);

	t= rl_inb(port, RL_CONFIG3);
	printk("CONFIG3: 0x%02lx\n", t);

	t= rl_inb(port, RL_CONFIG4);
	printk("CONFIG4: 0x%02lx\n", t);

	t= rl_inw(port, RL_BMCR);
	printk("BMCR (MII_CTRL): 0x%04lx\n", t);

	t= rl_inw(port, RL_BMSR);
	printk("BMSR:");
	if (t & MII_STATUS_100T4)
		printk(" 100Base-T4");
	if (t & MII_STATUS_100XFD)
		printk(" 100Base-X-FD");
	if (t & MII_STATUS_100XHD)
		printk(" 100Base-X-HD");
	if (t & MII_STATUS_10FD)
		printk(" 10Mbps-FD");
	if (t & MII_STATUS_10HD)
		printk(" 10Mbps-HD");
	if (t & MII_STATUS_100T2FD)
		printk(" 100Base-T2-FD");
	if (t & MII_STATUS_100T2HD)
		printk(" 100Base-T2-HD");
	if (t & MII_STATUS_EXT_STAT)
		printk(" Ext-stat");
	if (t & MII_STATUS_RES)
		printk(" res-0x%lx", t & MII_STATUS_RES);
	if (t & MII_STATUS_MFPS)
		printk(" MFPS");
	if (t & MII_STATUS_ANC)
		printk(" ANC");
	if (t & MII_STATUS_RF)
		printk(" remote-fault");
	if (t & MII_STATUS_ANA)
		printk(" ANA");
	if (t & MII_STATUS_LS)
		printk(" Link");
	if (t & MII_STATUS_JD)
		printk(" Jabber");
	if (t & MII_STATUS_EC)
		printk(" Extended-capability");
	printk("\n");

	t= rl_inw(port, RL_ANAR);
	printk("ANAR (MII_ANA): 0x%04lx\n", t);

	t= rl_inw(port, RL_ANLPAR);
	printk("ANLPAR: 0x%04lx\n", t);

	t= rl_inw(port, RL_ANER);
	printk("ANER (MII_ANE): ");
	if (t & MII_ANE_RES)
		printk(" res-0x%lx", t & MII_ANE_RES);
	if (t & MII_ANE_PDF)
		printk(" Par-Detect-Fault");
	if (t & MII_ANE_LPNPA)
		printk(" LP-Next-Page-Able");
	if (t & MII_ANE_NPA)
		printk(" Loc-Next-Page-Able");
	if (t & MII_ANE_PR)
		printk(" Page-Received");
	if (t & MII_ANE_LPANA)
		printk(" LP-Auto-Neg-Able");
	printk("\n");

	t= rl_inw(port, RL_NWAYTR);
	printk("NWAYTR: 0x%04lx\n", t);
	t= rl_inw(port, RL_CSCR);
	printk("CSCR: 0x%04lx\n", t);

	t= rl_inb(port, RL_CONFIG5);
	printk("CONFIG5: 0x%02lx\n", t);
}
#endif

static int do_hard_int(void)
{
	int i,s;

	for (i=0; i < RE_PORT_NR; i ++) {

		/* Run interrupt handler at driver level. */
		rl_handler( &re_table[i]);

		/* Reenable interrupts for this hook. */
	if ((s=sys_irqenable(&re_table[i].re_hook_id)) != 0)
		printk("RTL8139: error, couldn't enable interrupts: %d\n", s);
	}
}

/*===========================================================================*
 *				rl_handler				     *
 *===========================================================================*/
static int rl_handler(rep)
re_t *rep;
{
	int i, port, tx_head, tx_tail, link_up;
	u16_t isr, tsad;
	u32_t tsd, tcr, ertxth;
#if 0
	u8_t cr;
#endif
	clock_t t0,t1;
	int_event_check = FALSE;	/* disable check by default */

	port= rep->re_base_port;

	/* Ack interrupt */
	isr= rl_inw(port, RL_ISR);
	rl_outw(port, RL_ISR, isr);

	if (isr & RL_IMR_FOVW)
	{
		isr &= ~RL_IMR_FOVW;
		/* Should do anything? */

		rep->re_stat.ets_fifoOver++;
	}
	if (isr & RL_IMR_PUN)
	{
		isr &= ~RL_IMR_PUN;

		/* Either the link status changed or there was a TX fifo
		 * underrun.
		 */
		link_up= !(rl_inb(port, RL_MSR) & RL_MSR_LINKB);
		if (link_up != rep->re_link_up)
		{
			rep->re_report_link= TRUE;
			rep->re_got_int= TRUE;
			int_event_check = TRUE;
		}
	}
	if (isr & RL_IMR_RXOVW)
	{
		isr &= ~RL_IMR_RXOVW;

		/* Clear the receive buffer */
		rep->re_clear_rx= TRUE;
		rep->re_got_int= TRUE;
		int_event_check = TRUE;
	}

	if (isr & (RL_ISR_RER | RL_ISR_ROK))
	{
		isr &= ~(RL_ISR_RER | RL_ISR_ROK);

		if (!rep->re_got_int && (rep->re_flags & REF_READING))
		{
			rep->re_got_int= TRUE;
			int_event_check = TRUE;
		}
	}
#if 0
	if ((isr & (RL_ISR_TER | RL_ISR_TOK)) &&
		(rep->re_flags & REF_SEND_AVAIL) &&
		(rep->re_tx[0].ret_busy || rep->re_tx[1].ret_busy ||
		rep->re_tx[2].ret_busy || rep->re_tx[3].ret_busy))
		
	{
		printk(
	"rl_handler, SEND_AVAIL: tx_head %d, tx_tail %d, busy: %d %d %d %d\n",
			rep->re_tx_head, rep->re_tx_tail,
			rep->re_tx[0].ret_busy, rep->re_tx[1].ret_busy,
			rep->re_tx[2].ret_busy, rep->re_tx[3].ret_busy);
		printk(
	"rl_handler: TSAD: 0x%04x, TSD: 0x%08x, 0x%08x, 0x%08x, 0x%08x\n",
			rl_inw(port, RL_TSAD),
			rl_inl(port, RL_TSD0+0*4),
			rl_inl(port, RL_TSD0+1*4),
			rl_inl(port, RL_TSD0+2*4),
			rl_inl(port, RL_TSD0+3*4));
	}
#endif
	if ((isr & (RL_ISR_TER | RL_ISR_TOK)) || 1)
	{
		isr &= ~(RL_ISR_TER | RL_ISR_TOK);

		tsad= rl_inw(port, RL_TSAD);
		if (tsad & (RL_TSAD_TABT0|RL_TSAD_TABT1|
			RL_TSAD_TABT2|RL_TSAD_TABT3))
		{
#if 0
			/* Do we need a watch dog? */
			/* Just reset the whole chip */
			rep->re_need_reset= TRUE;
			rep->re_got_int= TRUE;
			int_event_check = TRUE;
#elif 0
			/* Reset transmitter */
			rep->re_stat.ets_transAb++;

			cr= rl_inb(port, RL_CR);
			cr &= ~RL_CR_TE;
			rl_outb(port, RL_CR, cr);
			getuptime(&t0);
			do {
				if (!(rl_inb(port, RL_CR) & RL_CR_TE))
					break;
			} while (getuptime(&t1)==0 && (t1-t0) < system_hz);
			if (rl_inb(port, RL_CR) & RL_CR_TE)
			{
			  panic("rtl8139","cannot disable transmitter",
					NO_NUM);
			}
			rl_outb(port, RL_CR, cr | RL_CR_TE);

			tcr= rl_inl(port, RL_TCR);
			rl_outl(port, RL_TCR, tcr | RL_TCR_IFG_STD);

			printk("rl_handler: reset after abort\n");

			if (rep->re_flags & REF_SEND_AVAIL)
			{
				printk("rl_handler: REF_SEND_AVAIL\n");
				rep->re_send_int= TRUE;
				rep->re_got_int= TRUE;
				int_event_check = TRUE;
			}
			for (i= 0; i< N_TX_BUF; i++)
				rep->re_tx[i].ret_busy= FALSE;
			rep->re_tx_head= 0;
#else
			printk("rl_handler, TABT, tasd = 0x%04x\n",
				tsad);

			/* Find the aborted transmit request */
			for (i= 0; i< N_TX_BUF; i++)
			{
				tsd= rl_inl(port, RL_TSD0+i*4);
				if (tsd & RL_TSD_TABT)
					break;
			}
			if (i >= N_TX_BUF)
			{
				printk(
				"rl_handler: can't find aborted TX req.\n");
			}
			else
			{
				printk("TSD%d = 0x%04lx\n", i, tsd);

				/* Set head and tail to this buffer */
				rep->re_tx_head= rep->re_tx_tail= i;
			}

			/* Aborted transmission, just kick the device
			 * and be done with it.
			 */
			rep->re_stat.ets_transAb++;
			tcr= rl_inl(port, RL_TCR);
			rl_outl(port, RL_TCR, tcr | RL_TCR_CLRABT);
#endif
		}

		/* Transmit completed */
		tx_head= rep->re_tx_head;
		tx_tail= rep->re_tx_tail;
		for (i= 0; i< 2*N_TX_BUF; i++)
		{
			if (!rep->re_tx[tx_tail].ret_busy)
			{
				/* Strange, this buffer is not in-use.
				 * Increment tx_tail until tx_head is
				 * reached (or until we find a buffer that
				 * is in-use.
				 */
				if (tx_tail == tx_head)
					break;
				if (++tx_tail >= N_TX_BUF)
					tx_tail= 0;
				assert(tx_tail < RL_N_TX);
				rep->re_tx_tail= tx_tail;
				continue;
			}
			tsd= rl_inl(port, RL_TSD0+tx_tail*4);
			if (!(tsd & RL_TSD_OWN))
			{
				/* Buffer is not yet ready */
				break;
			}

			/* Should collect statistics */
			if (tsd & RL_TSD_CRS)
				rep->re_stat.ets_carrSense++;
			if (tsd & RL_TSD_TABT)
			{
				printk("rl_handler, TABT, TSD%d = 0x%04lx\n",
					tx_tail, tsd);
				assert(0);	/* CLRABT is not all that
						 * effective, why not?
						 */
				rep->re_stat.ets_transAb++;
				tcr= rl_inl(port, RL_TCR);
				rl_outl(port, RL_TCR, tcr | RL_TCR_CLRABT);
			}
			if (tsd & RL_TSD_OWC)
				rep->re_stat.ets_OWC++;
			if (tsd & RL_TSD_CDH)
				rep->re_stat.ets_CDheartbeat++;

			/* What about collisions? */
			if (tsd & RL_TSD_TOK)
				rep->re_stat.ets_packetT++;
			else
				rep->re_stat.ets_sendErr++;
			if (tsd & RL_TSD_TUN)
			{
				rep->re_stat.ets_fifoUnder++;

				/* Increase ERTXTH */
				ertxth= tsd + (1 << RL_TSD_ERTXTH_S);
				ertxth &= RL_TSD_ERTXTH_M;
				if (debug && ertxth > rep->re_ertxth)
				{
					printk("%s: new ertxth: %ld bytes\n",
						rep->re_name,
						(ertxth >> RL_TSD_ERTXTH_S) *
						32);
					rep->re_ertxth= ertxth;
				}
			}
			rep->re_tx[tx_tail].ret_busy= FALSE;

#if 0
			if (rep->re_flags & REF_SEND_AVAIL)
			{
			printk("TSD%d: %08lx\n", tx_tail, tsd);
			printk(
			"rl_handler: head %d, tail %d, busy: %d %d %d %d\n", 
				tx_head, tx_tail,
				rep->re_tx[0].ret_busy, rep->re_tx[1].ret_busy, 
				rep->re_tx[2].ret_busy, rep->re_tx[3].ret_busy);
			}
#endif

			if (++tx_tail >= N_TX_BUF)
				tx_tail= 0;
			assert(tx_tail < RL_N_TX);
			rep->re_tx_tail= tx_tail;

			if (rep->re_flags & REF_SEND_AVAIL)
			{
#if 0
				printk("rl_handler: REF_SEND_AVAIL\n");
#endif
				rep->re_send_int= TRUE;
				if (!rep->re_got_int)
				{
					rep->re_got_int= TRUE;
					int_event_check = TRUE;
				}
			}
		}
		assert(i < 2*N_TX_BUF);
	}
	if (isr)
	{
		printk("rl_handler: unhandled interrupt: isr = 0x%04x\n",
			isr);
	}

	return 1;
}

/*===========================================================================*
 *				rl_watchdog_f				     *
 *===========================================================================*/
static void rl_watchdog_f(tp)
timer_t *tp;
{
	int i;
	re_t *rep;
	/* Use a synchronous alarm instead of a watchdog timer. */
	sys_setalarm(system_hz, 0);

	for (i= 0, rep = &re_table[0]; i<RE_PORT_NR; i++, rep++)
	{
		if (rep->re_mode != REM_ENABLED)
			continue;
		if (!(rep->re_flags & REF_SEND_AVAIL))
		{
			/* Assume that an idle system is alive */
			rep->re_tx_alive= TRUE;
			continue;
		}
		if (rep->re_tx_alive)
		{
			rep->re_tx_alive= FALSE;
			continue;
		}
		printk("rl_watchdog_f: resetting port %d\n", i);
		printk(
	"TSAD: 0x%04x, TSD: 0x%08x, 0x%08x, 0x%08x, 0x%08x\n",
			rl_inw(rep->re_base_port, RL_TSAD),
			rl_inl(rep->re_base_port, RL_TSD0+0*4),
			rl_inl(rep->re_base_port, RL_TSD0+1*4),
			rl_inl(rep->re_base_port, RL_TSD0+2*4),
			rl_inl(rep->re_base_port, RL_TSD0+3*4));
		printk("tx_head %d, tx_tail %d, busy: %d %d %d %d\n",
			rep->re_tx_head, rep->re_tx_tail,
			rep->re_tx[0].ret_busy, rep->re_tx[1].ret_busy,
			rep->re_tx[2].ret_busy, rep->re_tx[3].ret_busy);
		rep->re_need_reset= TRUE;
		rep->re_got_int= TRUE;
			
		check_int_events();
	}
}

#if 0

static void rtl_init(struct dpeth *dep);
static u16_t get_ee_word(dpeth_t *dep, int a);
static void ee_wen(dpeth_t *dep);
static void set_ee_word(dpeth_t *dep, int a, u16 w);
static void ee_wds(dpeth_t *dep);

static void rtl_init(dep)
dpeth_t *dep;
{
	u8_t reg_a, reg_b, cr, config0, config2, config3;
	int i;
	char val[128];

	printk("rtl_init called\n");
	ne_init(dep);

	/* ID */
	outb_reg0(dep, DP_CR, CR_PS_P0);
	reg_a = inb_reg0(dep, DP_DUM1);
	reg_b = inb_reg0(dep, DP_DUM2);

	printk("rtl_init: '%c', '%c'\n", reg_a, reg_b);

	outb_reg0(dep, DP_CR, CR_PS_P3);
	config0 = inb_reg3(dep, 3);
	config2 = inb_reg3(dep, 5);
	config3 = inb_reg3(dep, 6);
	outb_reg0(dep, DP_CR, CR_PS_P0);

	printk("rtl_init: config 0/2/3 = %x/%x/%x\n",
		config0, config2, config3);

	if (0 == sys_getkenv("RTL8029FD",9+1, val, sizeof(val)))
	{
		printk("rtl_init: setting full-duplex mode\n");
		outb_reg0(dep, DP_CR, CR_PS_P3);

		cr= inb_reg3(dep, 1);
		outb_reg3(dep, 1, cr | 0xc0);

		outb_reg3(dep, 6, config3 | 0x40);
		config3 = inb_reg3(dep, 6);

		config2= inb_reg3(dep, 5);
		outb_reg3(dep, 5, config2 | 0x20);
		config2= inb_reg3(dep, 5);

		outb_reg3(dep, 1, cr);

		outb_reg0(dep, DP_CR, CR_PS_P0);

		printk("rtl_init: config 2 = %x\n", config2);
		printk("rtl_init: config 3 = %x\n", config3);
	}

	for (i= 0; i<64; i++)
		printk("%x ", get_ee_word(dep, i));
	printk("\n");

	if (0 == sys_getkenv("RTL8029MN",9+1, val, sizeof(val)))
	{
		ee_wen(dep);

		set_ee_word(dep, 0x78/2, 0x10ec);
		set_ee_word(dep, 0x7A/2, 0x8029);
		set_ee_word(dep, 0x7C/2, 0x10ec);
		set_ee_word(dep, 0x7E/2, 0x8029);

		ee_wds(dep);

		assert(get_ee_word(dep, 0x78/2) == 0x10ec);
		assert(get_ee_word(dep, 0x7A/2) == 0x8029);
		assert(get_ee_word(dep, 0x7C/2) == 0x10ec);
		assert(get_ee_word(dep, 0x7E/2) == 0x8029);
	}

	if (0 == sys_getkenv("RTL8029XXX",10+1, val, sizeof(val)))
	{
		ee_wen(dep);

		set_ee_word(dep, 0x76/2, 0x8029);

		ee_wds(dep);

		assert(get_ee_word(dep, 0x76/2) == 0x8029);
	}
}

static u16_t get_ee_word(dep, a)
dpeth_t *dep;
int a;
{
	int b, i, cmd;
	u16_t w;

	outb_reg0(dep, DP_CR, CR_PS_P3);	/* Bank 3 */

	/* Switch to 9346 mode and enable CS */
	outb_reg3(dep, 1, 0x80 | 0x8);

	cmd= 0x180 | (a & 0x3f);	/* 1 1 0 a5 a4 a3 a2 a1 a0 */
	for (i= 8; i >= 0; i--)
	{
		b= (cmd & (1 << i));
		b= (b ? 2 : 0);

		/* Cmd goes out on the rising edge of the clock */
		outb_reg3(dep, 1, 0x80 | 0x8 | b);
		outb_reg3(dep, 1, 0x80 | 0x8 | 0x4 | b);
	}
	outb_reg3(dep, 1, 0x80 | 0x8);	/* End of cmd */

	w= 0;
	for (i= 0; i<16; i++)
	{
		w <<= 1;

		/* Data is shifted out on the rising edge. Read at the
		 * falling edge.
		 */
		outb_reg3(dep, 1, 0x80 | 0x8 | 0x4);
		outb_reg3(dep, 1, 0x80 | 0x8 | b);
		b= inb_reg3(dep, 1);
		w |= (b & 1);
	}

	outb_reg3(dep, 1, 0x80);		/* drop CS */
	outb_reg3(dep, 1, 0x00);		/* back to normal */
	outb_reg0(dep, DP_CR, CR_PS_P0);	/* back to bank 0 */

	return w;
}

static void ee_wen(dep)
dpeth_t *dep;
{
	int b, i, cmd;
	u16_t w;

	outb_reg0(dep, DP_CR, CR_PS_P3);	/* Bank 3 */

	/* Switch to 9346 mode and enable CS */
	outb_reg3(dep, 1, 0x80 | 0x8);

	cmd= 0x130;		/* 1 0 0 1 1 x x x x */
	for (i= 8; i >= 0; i--)
	{
		b= (cmd & (1 << i));
		b= (b ? 2 : 0);

		/* Cmd goes out on the rising edge of the clock */
		outb_reg3(dep, 1, 0x80 | 0x8 | b);
		outb_reg3(dep, 1, 0x80 | 0x8 | 0x4 | b);
	}
	outb_reg3(dep, 1, 0x80 | 0x8);	/* End of cmd */
	outb_reg3(dep, 1, 0x80);	/* Drop CS */
	/* micro_delay(1); */			/* Is this required? */
}

static void set_ee_word(dep, a, w)
dpeth_t *dep;
int a;
u16_t w;
{
	int b, i, cmd;
	clock_t t0, t1;

	outb_reg3(dep, 1, 0x80 | 0x8);		/* Set CS */

	cmd= 0x140 | (a & 0x3f);		/* 1 0 1 a5 a4 a3 a2 a1 a0 */
	for (i= 8; i >= 0; i--)
	{
		b= (cmd & (1 << i));
		b= (b ? 2 : 0);

		/* Cmd goes out on the rising edge of the clock */
		outb_reg3(dep, 1, 0x80 | 0x8 | b);
		outb_reg3(dep, 1, 0x80 | 0x8 | 0x4 | b);
	}
	for (i= 15; i >= 0; i--)
	{
		b= (w & (1 << i));
		b= (b ? 2 : 0);

		/* Cmd goes out on the rising edge of the clock */
		outb_reg3(dep, 1, 0x80 | 0x8 | b);
		outb_reg3(dep, 1, 0x80 | 0x8 | 0x4 | b);
	}
	outb_reg3(dep, 1, 0x80 | 0x8);	/* End of data */
	outb_reg3(dep, 1, 0x80);	/* Drop CS */
	/* micro_delay(1); */			/* Is this required? */
	outb_reg3(dep, 1, 0x80 | 0x8);		/* Set CS */
	getuptime(&t0);
	do {
		if (inb_reg3(dep, 1) & 1)
			break;
	} while (getuptime(&t1) == 0 && (t1 == t0));
	if (!(inb_reg3(dep, 1) & 1))
		panic("set_ee_word","device remains busy", NO_NUM);
}

static void ee_wds(dep)
dpeth_t *dep;
{
	int b, i, cmd;
	u16_t w;

	outb_reg0(dep, DP_CR, CR_PS_P3);	/* Bank 3 */

	/* Switch to 9346 mode and enable CS */
	outb_reg3(dep, 1, 0x80 | 0x8);

	cmd= 0x100;		/* 1 0 0 0 0 x x x x */
	for (i= 8; i >= 0; i--)
	{
		b= (cmd & (1 << i));
		b= (b ? 2 : 0);

		/* Cmd goes out on the rising edge of the clock */
		outb_reg3(dep, 1, 0x80 | 0x8 | b);
		outb_reg3(dep, 1, 0x80 | 0x8 | 0x4 | b);
	}
	outb_reg3(dep, 1, 0x80 | 0x8);	/* End of cmd */
	outb_reg3(dep, 1, 0x80);	/* Drop CS */
	outb_reg3(dep, 1, 0x00);		/* back to normal */
	outb_reg0(dep, DP_CR, CR_PS_P0);	/* back to bank 0 */
}
#endif

static void tell_dev(buf, size, pci_bus, pci_dev, pci_func)
vir_bytes buf;
size_t size;
int pci_bus;
int pci_dev;
int pci_func;
{
	int r;
	endpoint_t dev_e;
	u32_t u32;
	kipc_msg_t m;

	r= ds_retrieve_u32("amddev", &u32);
	if (r != 0)
	{
#if 0
		printk(
		"rtl8139`tell_dev: ds_retrieve_u32 failed for 'amddev': %d\n",
			r);
#endif
		return;
	}

	dev_e= u32;

	m.m_type= IOMMU_MAP;
	m.m_data1= pci_bus;
	m.m_data2= pci_dev;
	m.m_data3= pci_func;
	m.m_data4= buf;
	m.m_data5= size;

	r= kipc_module_call(KIPC_SENDREC, 0, dev_e, &m);
	if (r != 0)
	{
		printk("rtl8139`tell_dev: sendrec to %d failed: %d\n",
			dev_e, r);
		return;
	}
	if (m.m_type != 0)
	{
		printk("rtl8139`tell_dev: dma map request failed: %d\n",
			m.m_type);
		return;
	}
}
