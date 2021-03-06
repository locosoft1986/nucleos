/*
 *  Copyright (C) 2012  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
#include <nucleos/syslib.h>

/*===========================================================================*
 *                               sys_setalarm		     	     	     *
 *===========================================================================*/
int sys_setalarm(exp_time, abs_time)
clock_t exp_time;	/* expiration time for the alarm */
int abs_time;		/* use absolute or relative expiration time */
{
/* Ask the SYSTEM schedule a synchronous alarm for the caller. The process
 * number can be ENDPT_SELF if the caller doesn't know its process number.
 */
    kipc_msg_t m;
    m.ALRM_EXP_TIME = exp_time;		/* the expiration time */
    m.ALRM_ABS_TIME = abs_time;		/* time is absolute? */
    return ktaskcall(SYSTASK, SYS_SETALARM, &m);
}

