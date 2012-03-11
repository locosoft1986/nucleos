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
 * This file contains several functions and variables used for system
 * profiling.
 *
 * Statistical Profiling:
 *   The interrupt handler for profiling clock. 
 *
 * Call Profiling:
 *   The table used for profiling data and a function to get its size.
 *
 *   The function used by kernelspace processes to register the locations
 *   of their control struct and profiling table.
 *
 * Changes:
 *   14 Aug, 2006   Created, (Rogier Meurs)
 */
#include <nucleos/profile.h>
#include <nucleos/portio.h>
#include <nucleos/string.h>
#include <kernel/kernel.h>
#include <kernel/profile.h>
#include <kernel/proc.h>

#ifdef CONFIG_DEBUG_KERNEL_STATS_PROFILE

/* A hook for the profiling clock interrupt handler. */
static irq_hook_t profile_clock_hook;

static int profile_clock_handler(irq_hook_t *hook)
{
	/* This executes on every tick of the CMOS timer. */

	/* Are we profiling, and profiling memory not full? */
	if (!sprofiling || sprof_info.mem_used == -1)
		return (1);

	/* Check if enough memory available before writing sample. */
	if (sprof_info.mem_used + sizeof(sprof_info) > sprof_mem_size) {
		sprof_info.mem_used = -1;
		return(1);
	}

	/* All is OK */

	/* Idle process? */
	if (priv(proc_ptr)->s_proc_nr == IDLE) {
		sprof_info.idle_samples++;
	} else
		/* Runnable system process? */
		if (priv(proc_ptr)->s_flags & SYS_PROC && proc_is_runnable(proc_ptr)) {
			/* Note: k_reenter is always 0 here. */

			/* Store sample (process name and program counter). */
			data_copy(SYSTEM, (vir_bytes) proc_ptr->p_name, sprof_ep,
				  sprof_data_addr_vir + sprof_info.mem_used,
				  strlen(proc_ptr->p_name));

			data_copy(SYSTEM, (vir_bytes) &proc_ptr->p_reg.pc, sprof_ep,
				  (vir_bytes) (sprof_data_addr_vir + sprof_info.mem_used +
				  sizeof(proc_ptr->p_name)),
				  (vir_bytes) sizeof(proc_ptr->p_reg.pc));

			sprof_info.mem_used += sizeof(sprof_sample);

			sprof_info.system_samples++;
		} else {
			/* User process. */
			sprof_info.user_samples++;
		}

		sprof_info.total_samples++;

		/* Acknowledge interrupt if necessary. */
		arch_ack_profile_clock();

		return(1);	/* reenable interrupts */
}

void init_profile_clock(u32_t freq)
{
	int irq;

	intr_disable();

	if((irq = arch_init_profile_clock(freq)) >= 0) {
		/* Register interrupt handler for statistical system profiling.  */
		profile_clock_hook.proc_nr_e = CLOCK;
		put_irq_handler(&profile_clock_hook, irq, profile_clock_handler);
		enable_irq(&profile_clock_hook);
	}

	intr_enable();
}

void stop_profile_clock()
{
	intr_disable();
	arch_stop_profile_clock();
	intr_enable();

	/* Unregister interrupt handler. */
	disable_irq(&profile_clock_hook);
	rm_irq_handler(&profile_clock_hook);
}

#endif /* CONFIG_DEBUG_KERNEL_STATS_PROFILE */

#ifdef CONFIG_DEBUG_KERNEL_CALL_PROFILE
/* 
 * The following variables and functions are used by the procentry/
 * procentry syslib functions when linked with kernelspace processes.
 * For userspace processes, the same variables and function are defined
 * elsewhere. This enables different functionality and variable sizes,
 * which is needed is a few cases.
 */

/* A small table is declared for the kernelspace processes. */
struct cprof_tbl_s cprof_tbl[CPROF_TABLE_SIZE_KERNEL];

/* Function that returns table size. */
int profile_get_tbl_size(void)
{
	return CPROF_TABLE_SIZE_KERNEL;
}

/* Function that returns on which execution of procentry to announce. */
int profile_get_announce(void)
{
	return CPROF_ACCOUNCE_KERNEL;
}

/*
 * The kernel "announces" its control struct and table locations
 * to itself through this function.
 */
void profile_register(void *ctl_ptr, void *tbl_ptr)
{
	struct proc *rp;

	if(cprof_procs_no >= NR_SYS_PROCS)
		return;

	/* Store process name, control struct, table locations. */
	rp = proc_addr(SYSTEM);

	cprof_proc_info[cprof_procs_no].endpt = rp->p_endpoint;
	cprof_proc_info[cprof_procs_no].name = rp->p_name;
	cprof_proc_info[cprof_procs_no].ctl_v = (vir_bytes) ctl_ptr;
	cprof_proc_info[cprof_procs_no].buf_v = (vir_bytes) tbl_ptr;

	cprof_procs_no++;
}

#endif /* CONFIG_DEBUG_KERNEL_CALL_PROFILE */
