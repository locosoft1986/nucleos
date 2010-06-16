/*
 *  Copyright (C) 2009  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
#include <nucleos/drivers.h>
#include <nucleos/endpoint.h>
#include <nucleos/pci.h>
#include <ibm/pci.h>
#include <servers/rs/rs.h>

#define NR_DRIVERS	NR_SYS_PROCS

static struct acl
{
	int inuse;
	struct rs_pci acl;
} acl[NR_DRIVERS];

static void do_init(kipc_msg_t *mp);
static void do_first_dev(kipc_msg_t *mp);
static void do_next_dev(kipc_msg_t *mp);
static void do_find_dev(kipc_msg_t *mp);
static void do_ids(kipc_msg_t *mp);
static void do_dev_name(kipc_msg_t *mp);
static void do_dev_name_s(kipc_msg_t *mp);
static void do_slot_name_s(kipc_msg_t *mp);
static void do_set_acl(kipc_msg_t *mp);
static void do_del_acl(kipc_msg_t *mp);
static void do_reserve(kipc_msg_t *mp);
static void do_attr_r8(kipc_msg_t *mp);
static void do_attr_r16(kipc_msg_t *mp);
static void do_attr_r32(kipc_msg_t *mp);
static void do_attr_w8(kipc_msg_t *mp);
static void do_attr_w16(kipc_msg_t *mp);
static void do_attr_w32(kipc_msg_t *mp);
static void do_rescan_bus(kipc_msg_t *mp);
static void reply(kipc_msg_t *mp, int result);
static struct rs_pci *find_acl(int endpoint);

extern int debug;

int main(void)
{
	int i, r;
	kipc_msg_t m;

	pci_init();

	for(;;)
	{
		r= kipc_receive(ENDPT_ANY, &m);
		if (r < 0)
		{
			printf("PCI: receive from ENDPT_ANY failed: %d\n", r);
			break;
		}

		if (is_notify(m.m_type)) {
			switch (_ENDPOINT_P(m.m_source)) {
				case PM_PROC_NR:
					break;
				default:
					printf("PCI: got notify from %d\n",
								m.m_source);
					break;
			}

			/* done, get a new message */
			continue;
		}

		switch(m.m_type)
		{
		case BUSC_PCI_INIT: do_init(&m); break;
		case BUSC_PCI_FIRST_DEV: do_first_dev(&m); break;
		case BUSC_PCI_NEXT_DEV: do_next_dev(&m); break;
		case BUSC_PCI_FIND_DEV: do_find_dev(&m); break;
		case BUSC_PCI_IDS: do_ids(&m); break;
		case BUSC_PCI_DEV_NAME: do_dev_name(&m); break;
		case BUSC_PCI_RESERVE: do_reserve(&m); break;
		case BUSC_PCI_ATTR_R8: do_attr_r8(&m); break;
		case BUSC_PCI_ATTR_R16: do_attr_r16(&m); break;
		case BUSC_PCI_ATTR_R32: do_attr_r32(&m); break;
		case BUSC_PCI_ATTR_W8: do_attr_w8(&m); break;
		case BUSC_PCI_ATTR_W16: do_attr_w16(&m); break;
		case BUSC_PCI_ATTR_W32: do_attr_w32(&m); break;
		case BUSC_PCI_RESCAN: do_rescan_bus(&m); break;
		case BUSC_PCI_DEV_NAME_S: do_dev_name_s(&m); break;
		case BUSC_PCI_SLOT_NAME_S: do_slot_name_s(&m); break;
		case BUSC_PCI_SET_ACL: do_set_acl(&m); break;
		case BUSC_PCI_DEL_ACL: do_del_acl(&m); break;
		default:
			printf("PCI: got message from %d, type %d\n",
				m.m_source, m.m_type);
			break;
		}
	}

	return 0;
}

static void do_init(mp)
kipc_msg_t *mp;
{
	int r;

#if DEBUG
	printf("PCI: pci_init: called by '%d'\n", mp->m_source);
#endif

	mp->m_type= 0;
	r= kipc_send(mp->m_source, mp);
	if (r != 0)
		printf("PCI: do_init: unable to send to %d: %d\n",
			mp->m_source, r);
}

static void do_first_dev(mp)
kipc_msg_t *mp;
{
	int i, r, devind;
	u16_t vid, did;
	struct rs_pci *aclp;

	aclp= find_acl(mp->m_source);

	if (!aclp && debug)
		printf("PCI: do_first_dev: no acl for caller %d\n",
			mp->m_source);

	r= pci_first_dev_a(aclp, &devind, &vid, &did);
	if (r == 1)
	{
		mp->m1_i1= devind;
		mp->m1_i2= vid;
		mp->m1_i3= did;
	}
	mp->m_type= r;
	r= kipc_send(mp->m_source, mp);
	if (r != 0)
	{
		printf("PCI: do_first_dev: unable to kipc_send to %d: %d\n",
			mp->m_source, r);
	}
}

static void do_next_dev(mp)
kipc_msg_t *mp;
{
	int r, devind;
	u16_t vid, did;
	struct rs_pci *aclp;

	devind= mp->m1_i1;
	aclp= find_acl(mp->m_source);

	r= pci_next_dev_a(aclp, &devind, &vid, &did);
	if (r == 1)
	{
		mp->m1_i1= devind;
		mp->m1_i2= vid;
		mp->m1_i3= did;
	}
	mp->m_type= r;
	r= kipc_send(mp->m_source, mp);
	if (r != 0)
	{
		printf("PCI: do_next_dev: unable to kipc_send to %d: %d\n",
			mp->m_source, r);
	}
}

static void do_find_dev(mp)
kipc_msg_t *mp;
{
	int r, devind;
	u8_t bus, dev, func;

	bus= mp->m1_i1;
	dev= mp->m1_i2;
	func= mp->m1_i3;

	r= pci_find_dev(bus, dev, func, &devind);
	if (r == 1)
		mp->m1_i1= devind;
	mp->m_type= r;
	r= kipc_send(mp->m_source, mp);
	if (r != 0)
	{
		printf("PCI: do_find_dev: unable to kipc_send to %d: %d\n",
			mp->m_source, r);
	}
}

static void do_ids(mp)
kipc_msg_t *mp;
{
	int r, devind;
	u16_t vid, did;

	devind= mp->m1_i1;

	r= pci_ids_s(devind, &vid, &did);
	if (r != 0)
	{
		printf("pci:do_ids: failed for devind %d: %d\n",
			devind, r);
	}

	mp->m1_i1= vid;
	mp->m1_i2= did;
	mp->m_type= r;
	r= kipc_send(mp->m_source, mp);
	if (r != 0)
	{
		printf("PCI: do_ids: unable to kipc_send to %d: %d\n",
			mp->m_source, r);
	}
}

static void do_dev_name(mp)
kipc_msg_t *mp;
{
	int r, name_len, len;
	u16_t vid, did;
	char *name_ptr, *name;

	vid= mp->m1_i1;
	did= mp->m1_i2;
	name_len= mp->m1_i3;
	name_ptr= mp->m1_p1;

	name= pci_dev_name(vid, did);
	if (name == NULL)
	{
		/* No name */
		r= -ENOENT;
	}
	else
	{
		len= strlen(name)+1;
		if (len > name_len)
			len= name_len;
		printf("PCI: pci`do_dev_name: calling do_vircopy\n");
		r= sys_vircopy(ENDPT_SELF, D, (vir_bytes)name, mp->m_source, D,
			(vir_bytes)name_ptr, len);
	}

	mp->m_type= r;
	r= kipc_send(mp->m_source, mp);
	if (r != 0)
	{
		printf("PCI: do_dev_name: unable to kipc_send to %d: %d\n",
			mp->m_source, r);
	}
}

static void do_dev_name_s(mp)
kipc_msg_t *mp;
{
	int r, name_len, len;
	u16_t vid, did;
	cp_grant_id_t name_gid;
	char *name;

	vid= mp->m7_i1;
	did= mp->m7_i2;
	name_len= mp->m7_i3;
	name_gid= mp->m7_i4;

	name= pci_dev_name(vid, did);
	if (name == NULL)
	{
		/* No name */
		r= -ENOENT;
	}
	else
	{
		len= strlen(name)+1;
		if (len > name_len)
			len= name_len;
		r= sys_safecopyto(mp->m_source, name_gid, 0, (vir_bytes)name,
			len, D);
	}

	mp->m_type= r;
	r= kipc_send(mp->m_source, mp);
	if (r != 0)
	{
		printf("PCI: do_dev_name: unable to kipc_send to %d: %d\n",
			mp->m_source, r);
	}
}

static void do_slot_name_s(mp)
kipc_msg_t *mp;
{
	int r, devind, name_len, len;
	cp_grant_id_t gid;
	char *name;

	devind= mp->m1_i1;
	name_len= mp->m1_i2;
	gid= mp->m1_i3;

	r= pci_slot_name_s(devind, &name);
	if (r != 0)
	{
		printf("pci:do_slot_name_s: failed for devind %d: %d\n",
			devind, r);
	}

	if (r == 0)
	{
	len= strlen(name)+1;
	if (len > name_len)
		len= name_len;
		r= sys_safecopyto(mp->m_source, gid, 0,
			(vir_bytes)name, len, D);
	}

	mp->m_type= r;
	r= kipc_send(mp->m_source, mp);
	if (r != 0)
	{
		printf("PCI: do_slot_name: unable to kipc_send to %d: %d\n",
			mp->m_source, r);
	}
}

static void do_set_acl(mp)
kipc_msg_t *mp;
{
	int i, r, gid;

	if (mp->m_source != RS_PROC_NR)
	{
		printf("PCI: do_set_acl: not from RS\n");
		reply(mp, -EPERM);
		return;
	}

	for (i= 0; i<NR_DRIVERS; i++)
	{
		if (!acl[i].inuse)
			break;
	}
	if (i >= NR_DRIVERS)
	{
		printf("PCI: do_set_acl: table is full\n");
		reply(mp, -ENOMEM);
		return;
	}

	gid= mp->m1_i1;

	r= sys_safecopyfrom(mp->m_source, gid, 0, (vir_bytes)&acl[i].acl,
		sizeof(acl[i].acl), D);
	if (r != 0)
	{
		printf("PCI: do_set_acl: safecopyfrom failed\n");
		reply(mp, r);
		return;
	}
	acl[i].inuse= 1;
	if(debug)
	  printf("PCI: do_acl: setting ACL for %d ('%s') at entry %d\n",
		acl[i].acl.rsp_endpoint, acl[i].acl.rsp_label,
		i);

	reply(mp, 0);
}

static void do_del_acl(mp)
kipc_msg_t *mp;
{
	int i, r, proc_nr;

	if (mp->m_source != RS_PROC_NR)
	{
		printf("do_del_acl: not from RS\n");
		reply(mp, -EPERM);
		return;
	}

	proc_nr= mp->m1_i1;

	for (i= 0; i<NR_DRIVERS; i++)
	{
		if (!acl[i].inuse)
			continue;
		if (acl[i].acl.rsp_endpoint == proc_nr)
			break;
	}

	if (i >= NR_DRIVERS)
	{
		printf("do_del_acl: nothing found for %d\n", proc_nr);
		reply(mp, -EINVAL);
		return;
	}

	acl[i].inuse= 0;
#if 0
	printf("do_acl: deleting ACL for %d ('%s') at entry %d\n",
		acl[i].acl.rsp_endpoint, acl[i].acl.rsp_label, i);
#endif

	/* Also release all devices held by this process */
	pci_release(proc_nr);

	reply(mp, 0);
}

static void do_reserve(mp)
kipc_msg_t *mp;
{
	int i, r, devind;

	devind= mp->m1_i1;

	mp->m_type= pci_reserve2(devind, mp->m_source);
	r= kipc_send(mp->m_source, mp);
	if (r != 0)
	{
		printf("do_reserve: unable to kipc_send to %d: %d\n",
			mp->m_source, r);
	}
}

static void do_attr_r8(mp)
kipc_msg_t *mp;
{
	int r, devind, port;
	u8_t v;

	devind= mp->m2_i1;
	port= mp->m2_i2;

	r= pci_attr_r8_s(devind, port, &v);
	if (r != 0)
	{
		printf(
		"pci:do_attr_r8: pci_attr_r8_s(%d, %d, ...) failed: %d\n",
			devind, port, r);
	}
	mp->m2_l1= v;
	mp->m_type= r;
	r= kipc_send(mp->m_source, mp);
	if (r != 0)
	{
		printf("do_attr_r8: unable to kipc_send to %d: %d\n",
			mp->m_source, r);
	}
}

static void do_attr_r16(mp)
kipc_msg_t *mp;
{
	int r, devind, port;
	u32_t v;

	devind= mp->m2_i1;
	port= mp->m2_i2;

	v= pci_attr_r16(devind, port);
	mp->m2_l1= v;
	mp->m_type= 0;
	r= kipc_send(mp->m_source, mp);
	if (r != 0)
	{
		printf("do_attr_r16: unable to kipc_send to %d: %d\n",
			mp->m_source, r);
	}
}

static void do_attr_r32(mp)
kipc_msg_t *mp;
{
	int r, devind, port;
	u32_t v;

	devind= mp->m2_i1;
	port= mp->m2_i2;

	r= pci_attr_r32_s(devind, port, &v);
	if (r != 0)
	{
		printf(
		"pci:do_attr_r32: pci_attr_r32_s(%d, %d, ...) failed: %d\n",
			devind, port, r);
	}
	mp->m2_l1= v;
	mp->m_type= 0;
	r= kipc_send(mp->m_source, mp);
	if (r != 0)
	{
		printf("do_attr_r32: unable to kipc_send to %d: %d\n",
			mp->m_source, r);
	}
}

static void do_attr_w8(mp)
kipc_msg_t *mp;
{
	int r, devind, port;
	u8_t v;

	devind= mp->m2_i1;
	port= mp->m2_i2;
	v= mp->m2_l1;

	pci_attr_w8(devind, port, v);
	mp->m_type= 0;
	r= kipc_send(mp->m_source, mp);
	if (r != 0)
	{
		printf("do_attr_w8: unable to send to %d: %d\n",
			mp->m_source, r);
	}
}

static void do_attr_w16(mp)
kipc_msg_t *mp;
{
	int r, devind, port;
	u16_t v;

	devind= mp->m2_i1;
	port= mp->m2_i2;
	v= mp->m2_l1;

	pci_attr_w16(devind, port, v);
	mp->m_type= 0;
	r= kipc_send(mp->m_source, mp);
	if (r != 0)
	{
		printf("do_attr_w16: unable to send to %d: %d\n",
			mp->m_source, r);
	}
}

static void do_attr_w32(mp)
kipc_msg_t *mp;
{
	int r, devind, port;
	u32_t v;

	devind= mp->m2_i1;
	port= mp->m2_i2;
	v= mp->m2_l1;

	pci_attr_w32(devind, port, v);
	mp->m_type= 0;
	r= kipc_send(mp->m_source, mp);
	if (r != 0)
	{
		printf("do_attr_w32: unable to send to %d: %d\n",
			mp->m_source, r);
	}
}

static void do_rescan_bus(mp)
kipc_msg_t *mp;
{
	int r, busnr;

	busnr= mp->m2_i1;

	pci_rescan_bus(busnr);
	mp->m_type= 0;
	r= kipc_send(mp->m_source, mp);
	if (r != 0)
	{
		printf("do_rescan_bus: unable to send to %d: %d\n",
			mp->m_source, r);
	}
}


static void reply(mp, result)
kipc_msg_t *mp;
int result;
{
	int r;
	kipc_msg_t m;

	m.m_type= result;
	r= kipc_send(mp->m_source, &m);
	if (r != 0)
		printf("reply: unable to send to %d: %d\n", mp->m_source, r);
}


static struct rs_pci *find_acl(endpoint)
int endpoint;
{
	int i;

	/* Find ACL entry for caller */
	for (i= 0; i<NR_DRIVERS; i++)
	{
		if (!acl[i].inuse)
			continue;
		if (acl[i].acl.rsp_endpoint == endpoint)
			return &acl[i].acl;
	}
	return NULL;
}
