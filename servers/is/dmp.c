/*
 *  Copyright (C) 2012  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
/* This file contains information dump procedures. During the initialization 
 * of the Information Service 'known' function keys are registered at the TTY
 * server in order to receive a notification if one is pressed. Here, the 
 * corresponding dump procedure is called.  
 *
 * The entry points into this file are
 *   map_unmap_fkeys:	register or unregister function key maps with TTY
 *   do_fkey_pressed:	handle a function key pressed notification
 */

#include "inc.h"
#include <nucleos/vm.h>

struct hook_entry {
	int key;
	void (*function)(void);
	char *name;
} hooks[] = {
	{ F1, 	proctab_dmp, "Kernel process table" },
	{ F2,   memmap_dmp, "Process memory maps" },
	{ F3,	image_dmp, "System image" },
	{ F4,	privileges_dmp, "Process privileges" },
	{ F5,	cmdline_params_dmp, "Kernel command-line parameters" },
	{ F6,	irqtab_dmp, "IRQ hooks and policies" },
	{ F7,	kmessages_dmp, "Kernel messages" },
	{ F8,	vm_dmp, "VM status" },
	{ F10,	kenv_dmp, "Kernel parameters" },
	{ F11,	timing_dmp, "Timing details (if enabled)" },
	{ SF1,	mproc_dmp, "Process manager process table" },
	{ SF2,	sigaction_dmp, "Signals" },
	{ SF3,	fproc_dmp, "Filesystem process table" },
	{ SF4,	dtab_dmp, "Device/Driver mapping" },
	{ SF5,	mapping_dmp, "Print key mappings" },
	{ SF6,	rproc_dmp, "Reincarnation server process table" },
	{ SF8,  data_store_dmp, "Data store contents" },
	{ SF9,  procstack_dmp, "Processes with stack traces" },
};

/* Define hooks for the debugging dumps. This table maps function keys
 * onto a specific dump and provides a description for it.
 */
#define NHOOKS (sizeof(hooks)/sizeof(hooks[0]))

/*===========================================================================*
 *				map_unmap_keys				     *
 *===========================================================================*/
void map_unmap_fkeys(map)
int map;
{
  int fkeys, sfkeys;
  int h, s;

  fkeys = sfkeys = 0;

  for (h = 0; h < NHOOKS; h++) {
      if (hooks[h].key >= F1 && hooks[h].key <= F12) 
          bit_set(fkeys, hooks[h].key - F1 + 1);
      else if (hooks[h].key >= SF1 && hooks[h].key <= SF12)
          bit_set(sfkeys, hooks[h].key - SF1 + 1);
  }

  if (map) s = fkey_map(&fkeys, &sfkeys);
  else s = fkey_unmap(&fkeys, &sfkeys);

  if (s != 0)
	report("IS", "warning, fkey_ctl failed:", s);
}

/*===========================================================================*
 *				handle_fkey				     *
 *===========================================================================*/
#define pressed(k) ((F1<=(k)&&(k)<=F12 && bit_isset(m->FKEY_FKEYS,((k)-F1+1)))\
  	|| (SF1<=(k) && (k)<=SF12 && bit_isset(m->FKEY_SFKEYS, ((k)-SF1+1)))) 
int do_fkey_pressed(m)
kipc_msg_t *m;					/* notification message */
{
  int s, h;

  /* The notification message does not convey any information, other
   * than that some function keys have been pressed. Ask TTY for details.
   */
  m->m_type = FKEY_CONTROL;
  m->FKEY_REQUEST = FKEY_EVENTS;
  if ((s=kipc_module_call(KIPC_SENDREC, 0, TTY_PROC_NR, m)) != 0)
      report("IS", "warning, sendrec to TTY failed", s);

  /* Now check which keys were pressed: F1-F12, SF1-SF12. */
  for(h=0; h < NHOOKS; h++)
      if(pressed(hooks[h].key))
          hooks[h].function();

  /* Don't send a reply message. */
  return(-EDONTREPLY);
}

/*===========================================================================*
 *				key_name				     *
 *===========================================================================*/
static char *key_name(int key)
{
	static char name[15];

	if(key >= F1 && key <= F12)
		sprintf(name, " F%d", key - F1 + 1);
	else if(key >= SF1 && key <= SF12)
		sprintf(name, "Shift+F%d", key - SF1 + 1);
	else
		sprintf(name, "?");
	return name;
}


/*===========================================================================*
 *				mapping_dmp				     *
 *===========================================================================*/
void mapping_dmp(void)
{
  int h;

  printk("Function key mappings for debug dumps in IS server.\n");
  printk("        Key   Description\n");
  printk("-------------------------------------");
  printk("------------------------------------\n");

  for(h=0; h < NHOOKS; h++)
      printk(" %10s.  %s\n", key_name(hooks[h].key), hooks[h].name);
  printk("\n");
}

/*===========================================================================*
 *				vm_dmp				     *
 *===========================================================================*/
void vm_dmp(void)
{
	vm_ctl(VCTLP_STATS_MEM, 0);

}
