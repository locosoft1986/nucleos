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
pci_slot_name.c
*/
#include <nucleos/syslib.h>
#include <nucleos/sysutil.h>
#include <nucleos/pci.h>

/*===========================================================================*
 *				pci_slot_name				     *
 *===========================================================================*/
char *pci_slot_name(devind)
int devind;
{
	static char name[80];	/* We need a better interface for this */

	int r;
	cp_grant_id_t gid;
	kipc_msg_t m;

	gid= cpf_grant_direct(pci_procnr, (vir_bytes)name, sizeof(name),
		CPF_WRITE);
	if (gid == -1)
	{
		printk("pci_dev_name: cpf_grant_direct failed: %d\n",
			errno);
		return NULL;
	}

	m.m_type= BUSC_PCI_SLOT_NAME_S;
	m.m_data1= devind;
	m.m_data2= sizeof(name);
	m.m_data3= gid;

	r= kipc_module_call(KIPC_SENDREC, 0, pci_procnr, &m);
	cpf_revoke(gid);
	if (r != 0)
		panic("syslib/" __FILE__, "pci_slot_name: can't talk to PCI", r);

	if (m.m_type != 0)
		panic("syslib/" __FILE__, "pci_slot_name: got bad reply from PCI", m.m_type);

	name[sizeof(name)-1]= '\0';	/* Make sure that the string is NUL
					 * terminated.
					 */
	return name;
}

