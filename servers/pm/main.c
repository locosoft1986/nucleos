/*
 *  Copyright (C) 2012  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
/* This file contains the main program of the process manager and some related
 * procedures.  When MINIX starts up, the kernel runs for a little while,
 * initializing itself and its tasks, and then it runs PM and FS.  Both PM
 * and FS initialize themselves as far as they can. PM asks the kernel for
 * all free memory and starts serving requests.
 *
 * The entry points into this file are:
 *   main:	starts PM running
 *   setreply:	set the reply to be sent to process making an PM system call
 */
#include <nucleos/kernel.h>
#include "pm.h"
#include <nucleos/keymap.h>
#include <nucleos/unistd.h>
#include <nucleos/com.h>
#include <servers/ds/ds.h>
#include <nucleos/type.h>
#include <nucleos/endpoint.h>
#include <nucleos/minlib.h>
#include <nucleos/type.h>
#include <nucleos/vm.h>
#include <nucleos/mman.h>
#include <nucleos/signal.h>
#include <stdlib.h>
#include <nucleos/fcntl.h>
#include <nucleos/resource.h>
#include <nucleos/utsname.h>
#include <nucleos/string.h>
#include <asm/kernel/const.h>
#include <asm/kernel/types.h>
#include <nucleos/sysutil.h>
#include <servers/pm/mproc.h>
#include <kernel/const.h>
#include <kernel/proc.h>

#include "param.h"

struct mproc mproc[NR_PROCS];

#ifdef CONFIG_DEBUG_SERVERS_SYSCALL_STATS
extern unsigned long calls_stats[NR_syscalls];
#endif

static void get_work(void);
static void pm_init(void);
static int get_nice_value(int queue);
static void handle_fs_reply(void);

#define click_to_round_k(n) \
	((unsigned) ((((unsigned long) (n) << CLICK_SHIFT) + 512) / 1024))

/*===========================================================================*
 *				main					     *
 *===========================================================================*/
int main()
{
/* Main routine of the process manager. */
  int result, s, proc_nr;
  struct mproc *rmp;
  sigset_t sigset;

  pm_init();			/* initialize process manager tables */

  /* This is PM's main loop-  get work and do it, forever and forever. */
  while (TRUE) {
	get_work();		/* wait for an PM system call */

	/* Drop delayed calls from exiting processes. */
	if (mp->mp_flags & EXITING)
		continue;

	/* Check for system notifications first. Special cases. */
	if (is_notify(call_nr)) {
		switch(who_p) {
		case CLOCK:
			pm_expire_timers(m_in.NOTIFY_TIMESTAMP);
			result = SUSPEND;       /* don't reply */
			break;
		case SYSTEM:                    /* signals pending */
			sigset = m_in.NOTIFY_ARG;
			if (sigismember(&sigset, SIGKSIG))  {
				(void) ksig_pending();
			}
			result = SUSPEND;       /* don't reply */
			break;
		default :
			result = -ENOSYS;
		}

		/* done, send reply and continue */
		goto send_reply;
	}

	switch(call_nr)
	{
	case PM_SETUID_REPLY:
	case PM_SETGID_REPLY:
	case PM_SETSID_REPLY:
	case PM_EXEC_REPLY:
	case PM_EXIT_REPLY:
	case PM_CORE_REPLY:
	case PM_FORK_REPLY:
	case PM_FORK_NB_REPLY:
	case PM_UNPAUSE_REPLY:
	case PM_REBOOT_REPLY:
	case PM_SETGROUPS_REPLY:
		if (who_e == VFS_PROC_NR)
		{
			handle_fs_reply();
			result= SUSPEND;		/* don't reply */
		} else
			result= -ENOSYS;
		break;
	case KCNR_ALLOCMEM:
		result= do_allocmem();
		break;
	case KCNR_FORK_NB:
		result= do_fork_nb();
		break;
	case KCNR_EXEC_NEWMEM:
		result= exec_newmem();
		break;
	case KCNR_EXEC_RESTART:
		result= do_execrestart();
		break;
	case KCNR_PROCSTAT:
		result= do_procstat();
		break;
	case KCNR_GETPROCNR:
		result= do_getprocnr();
		break;
	case KCNR_GETEPINFO:
		result= do_getepinfo();
		break;
	default:
		/* Else, if the system call number is valid, perform the
		 * call.
		 */
		if ((unsigned) call_nr >= NR_syscalls) {
			result = -ENOSYS;
		} else {
#ifdef CONFIG_DEBUG_SERVERS_SYSCALL_STATS
			calls_stats[call_nr]++;
#endif
			result = (*call_vec[call_nr])();
		}
		break;
	}

send_reply:
	/* Send the results back to the user to indicate completion. */
	if (result != SUSPEND) setreply(who_p, result);

	/* Send out all pending reply messages, including the answer to
	 * the call just made above.
	 */
	for (proc_nr=0, rmp=mproc; proc_nr < NR_PROCS; proc_nr++, rmp++) {
		/* In the meantime, the process may have been killed by a
		 * signal (e.g. if a lethal pending signal was unblocked)
		 * without the PM realizing it. If the slot is no longer in
		 * use or the process is exiting, don't try to reply.
		 */
		if ((rmp->mp_flags & (REPLY | IN_USE | EXITING)) ==
		   (REPLY | IN_USE)) {
			s=kipc_module_call(KIPC_SEND, KIPC_FLG_NONBLOCK, rmp->mp_endpoint, &rmp->mp_reply);
			if (s != 0) {
				printk("PM can't reply to %d (%s): %d\n",
					rmp->mp_endpoint, rmp->mp_name, s);
			}
			rmp->mp_flags &= ~REPLY;
		}
	}
  }
  return 0;
}

/*===========================================================================*
 *				get_work				     *
 *===========================================================================*/
static void get_work()
{
	/* Wait for the next message and extract useful information from it. */
	if (kipc_module_call(KIPC_RECEIVE, 0, ENDPT_ANY, &m_in) != 0)
		panic(__FILE__,"PM receive error", NO_NUM);

	who_e = m_in.m_source;	/* who sent the message */

	if(pm_isokendpt(who_e, &who_p) != 0)
		panic(__FILE__, "PM got message from invalid endpoint", who_e);

	call_nr = m_in.m_type;	/* system call number */

	/* Process slot of caller. Misuse PM's own process slot if the kernel is
	 * calling. This can happen in case of synchronous alarms (CLOCK) or or 
	 * event like pending kernel signals (SYSTEM).
	 */
	mp = &mproc[who_p < 0 ? PM_PROC_NR : who_p];

	if(who_p >= 0 && mp->mp_endpoint != who_e) {
		panic(__FILE__, "PM endpoint number out of sync with source", mp->mp_endpoint);
	}
}

/*===========================================================================*
 *				setreply				     *
 *===========================================================================*/
void setreply(proc_nr, result)
int proc_nr;			/* process to reply to */
int result;			/* result of call (usually 0 or error #) */
{
/* Fill in a reply message to be sent later to a user process.  System calls
 * may occasionally fill in other fields, this is only for the main return
 * value, and for setting the "must send reply" flag.
 */
  register struct mproc *rmp = &mproc[proc_nr];

  if(proc_nr < 0 || proc_nr >= NR_PROCS)
      panic(__FILE__,"setreply arg out of range", proc_nr);

  rmp->mp_reply.reply_res = result;
  rmp->mp_flags |= REPLY;	/* reply pending */
}

extern int unmap_ok;

/*===========================================================================*
 *				pm_init					     *
 *===========================================================================*/
static void pm_init()
{
/* Initialize the process manager. 
 * Memory use info is collected from the boot monitor, the kernel, and
 * all processes compiled into the system image. Initially this information
 * is put into an array mem_chunks. Elements of mem_chunks are struct memory,
 * and hold base, size pairs in units of clicks. This array is small, there
 * should be no more than 8 chunks. After the array of chunks has been built
 * the contents are used to initialize the hole list. Space for the hole list
 * is reserved as an array with twice as many elements as the maximum number
 * of processes allowed. It is managed as a linked list, and elements of the
 * array are struct hole, which, in addition to storage for a base and size in 
 * click units also contain space for a link, a pointer to another element.
*/
  int s;
  static struct boot_image image[NR_BOOT_PROCS];
  register struct boot_image *ip;
  static char core_sigs[] = { SIGQUIT, SIGILL, SIGTRAP, SIGABRT,
				SIGEMT, SIGFPE, SIGBUS, SIGSEGV };
  static char ign_sigs[] = { SIGCHLD, SIGWINCH, SIGCONT };
  static char mess_sigs[] = { SIGTERM, SIGHUP, SIGABRT, SIGQUIT };
  register struct mproc *rmp;
  register char *sig_ptr;
  kipc_msg_t mess;

  /* Initialize process table, including timers. */
  for (rmp=&mproc[0]; rmp<&mproc[NR_PROCS]; rmp++) {
	tmr_inittimer(&rmp->mp_timer);
  }

  /* Build the set of signals which cause core dumps, and the set of signals
   * that are by default ignored.
   */
  sigemptyset(&core_sset);
  for (sig_ptr = core_sigs; sig_ptr < core_sigs+sizeof(core_sigs); sig_ptr++)
	sigaddset(&core_sset, *sig_ptr);
  sigemptyset(&ign_sset);
  for (sig_ptr = ign_sigs; sig_ptr < ign_sigs+sizeof(ign_sigs); sig_ptr++)
	sigaddset(&ign_sset, *sig_ptr);

  /* Obtain a copy of the boot monitor parameters and the kernel info struct.  
   * Parse the list of free memory chunks. This list is what the boot monitor 
   * reported, but it must be corrected for the kernel and system processes.
   */
  if ((s=sys_get_cmdline_params(cmd_line_params, sizeof(cmd_line_params))) != 0)
      panic(__FILE__,"get command-line params failed",s);
  if ((s=sys_getkinfo(&kinfo)) != 0)
      panic(__FILE__,"get kernel info failed",s);

  /* Initialize PM's process table. Request a copy of the system image table 
   * that is defined at the kernel level to see which slots to fill in.
   */
  if ((s=sys_getimage(image)) != 0) 
  	panic(__FILE__,"couldn't get image table: %d\n", s);
  procs_in_use = 0;				/* start populating table */
  for (ip = &image[0]; ip < &image[NR_BOOT_PROCS]; ip++) {		
  	if (ip->proc_nr >= 0) {			/* task have negative nrs */
  		procs_in_use += 1;		/* found user process */

		/* Set process details found in the image table. */
		rmp = &mproc[ip->proc_nr];	
  		strncpy(rmp->mp_name, ip->proc_name, PROC_NAME_LEN); 
		rmp->mp_nice = get_nice_value(ip->priority);
  		sigemptyset(&rmp->mp_sig2mess);
  		sigemptyset(&rmp->mp_ignore);	
  		sigemptyset(&rmp->mp_sigmask);
  		sigemptyset(&rmp->mp_catch);
		if (ip->proc_nr == INIT_PROC_NR) {	/* user process */
  			/* INIT is root, we make it father of itself. This is
  			 * not really 0, INIT should have no father, i.e.
  			 * a father with pid NO_PID. But PM currently assumes 
  			 * that mp_parent always points to a valid slot number.
  			 */
  			rmp->mp_parent = INIT_PROC_NR;
  			rmp->mp_procgrp = rmp->mp_pid = INIT_PID;
			rmp->mp_flags |= IN_USE; 
		}
		else {					/* system process */
  			if(ip->proc_nr == RS_PROC_NR) {
  				rmp->mp_parent = INIT_PROC_NR;
  			}
  			else {
  				rmp->mp_parent = RS_PROC_NR;
  			}
  			rmp->mp_pid = get_free_pid();
			rmp->mp_flags |= IN_USE | PRIV_PROC; 
  			for (sig_ptr = mess_sigs; 
				sig_ptr < mess_sigs+sizeof(mess_sigs); 
				sig_ptr++)
			sigaddset(&rmp->mp_sig2mess, *sig_ptr);
		}

		/* Get kernel endpoint identifier. */
		rmp->mp_endpoint = ip->endpoint;

		/* Tell FS about this system process. */
		mess.PR_SLOT = ip->proc_nr;
		mess.PR_PID = rmp->mp_pid;
		mess.PR_ENDPT = rmp->mp_endpoint;
  		if ((s=kipc_module_call(KIPC_SEND, 0, VFS_PROC_NR, &mess)) != 0)
			panic(__FILE__,"can't sync up with FS", s);
  	}
  }

  /* Override some details for PM. */
  sigfillset(&mproc[PM_PROC_NR].mp_ignore); 	/* guard against signals */

  /* Tell FS that no more system processes follow and synchronize. */
  mess.PR_ENDPT = ENDPT_NONE;
  if (kipc_module_call(KIPC_SENDREC, 0, VFS_PROC_NR, &mess) != 0 || mess.m_type != 0)
	panic(__FILE__,"can't sync up with FS", NO_NUM);

#ifdef CONFIG_X86_32
        uts_val.machine[0] = 'i';
        strcpy(uts_val.machine + 1, itoa(getprocessor()));
#endif

 system_hz = sys_hz();

 /* Map out our own text and data. This is normally done in crtso.o
  * but PM is an exception - we don't get to talk to VM so early on.
  * That's why we override munmap() and munmap_text() in utility.c.
  *
  * unmap_page_zero() is the same code in crtso.o that normally does
  * it on startup. It's best that it's there as crtso.o knows exactly
  * what the ranges are of the filler data.
  */
  unmap_ok = 1;
  unmap_page_zero();
}

/*===========================================================================*
 *				get_nice_value				     *
 *===========================================================================*/
static int get_nice_value(queue)
int queue;				/* store mem chunks here */
{
/* Processes in the boot image have a priority assigned. The PM doesn't know
 * about priorities, but uses 'nice' values instead. The priority is between 
 * MIN_USER_Q and MAX_USER_Q. We have to scale between PRIO_MIN and PRIO_MAX.
 */ 
  int nice_val = (queue - USER_Q) * (PRIO_MAX-PRIO_MIN+1) / 
      (MIN_USER_Q-MAX_USER_Q+1);
  if (nice_val > PRIO_MAX) nice_val = PRIO_MAX;	/* shouldn't happen */
  if (nice_val < PRIO_MIN) nice_val = PRIO_MIN;	/* shouldn't happen */
  return nice_val;
}

void checkme(char *str, int line)
{
	struct mproc *trmp;
	int boned = 0;
	int proc_nr;
	for (proc_nr=0, trmp=mproc; proc_nr < NR_PROCS; proc_nr++, trmp++) {
		if ((trmp->mp_flags & (REPLY | IN_USE | EXITING)) ==
		   (REPLY | IN_USE)) {
			int tp;
			if(pm_isokendpt(trmp->mp_endpoint, &tp) != 0) {
			   printk("PM: %s:%d: reply %d to %s is bogus endpoint %d after call %d by %d\n",
				str, line, trmp->mp_reply.m_type,
				trmp->mp_name, trmp->mp_endpoint, call_nr, who_e);
			   boned=1;
			}
		}
		if(boned) panic(__FILE__, "corrupt mp_endpoint?", NO_NUM);
	}
}

/*===========================================================================*
 *				handle_fs_reply				     *
 *===========================================================================*/
static void handle_fs_reply()
{
  struct mproc *rmp;
  endpoint_t proc_e;
  int r, proc_n;

  /* PM_REBOOT is the only request not associated with a process.
   * Handle its reply first.
   */
  if (call_nr == PM_REBOOT_REPLY) {
	/* Ask the kernel to abort. All system services, including
	 * the PM, will get a HARD_STOP notification. Await the
	 * notification in the main loop.
	 */
	sys_abort(abort_flag);

	return;
  }

  /* Get the process associated with this call */
  proc_e = m_in.PM_PROC;

  if (pm_isokendpt(proc_e, &proc_n) != 0) {
	panic(__FILE__, "handle_fs_reply: got bad endpoint from FS", proc_e);
  }

  rmp = &mproc[proc_n];

  /* Now that FS replied, mark the process as FS-idle again */
  if (!(rmp->mp_flags & FS_CALL))
	panic(__FILE__, "handle_fs_reply: reply without request", call_nr);

  rmp->mp_flags &= ~FS_CALL;

  if (rmp->mp_flags & UNPAUSED)
  	panic(__FILE__, "handle_fs_reply: UNPAUSED set on entry", call_nr);

  /* Call-specific handler code */
  switch (call_nr) {
  case PM_SETUID_REPLY:
  case PM_SETGID_REPLY:
  case PM_SETGROUPS_REPLY:
	/* Wake up the original caller */
	setreply(rmp-mproc, 0);

	break;

  case PM_SETSID_REPLY:
	/* Wake up the original caller */
	setreply(rmp-mproc, rmp->mp_procgrp);

	break;

  case PM_EXEC_REPLY:
	exec_restart(rmp, m_in.PM_STATUS);

	break;

  case PM_EXIT_REPLY:
	exit_restart(rmp, FALSE /*dump_core*/);

	break;

  case PM_CORE_REPLY:
	if (m_in.PM_STATUS == 0)
		rmp->mp_sigstatus |= DUMPED;

	exit_restart(rmp, TRUE /*dump_core*/);

	break;

  case PM_FORK_REPLY:
	/* Wake up the newly created process */
	setreply(proc_n, 0);

	/* Wake up the parent */
	setreply(rmp->mp_parent, rmp->mp_pid);

	break;

  case PM_FORK_NB_REPLY:
	/* Nothing to do */

	break;

  case PM_UNPAUSE_REPLY:
	/* Process is now unpaused */
	rmp->mp_flags |= UNPAUSED;

	break;

  default:
	panic(__FILE__, "handle_fs_reply: unknown reply code", call_nr);
  }

  /* Now that the process is idle again, look at pending signals */
  if ((rmp->mp_flags & (IN_USE | EXITING)) == IN_USE)
	  restart_sigs(rmp);
}
