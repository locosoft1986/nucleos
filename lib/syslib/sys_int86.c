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

int sys_int86(struct reg86u *reg86p)
{
	kipc_msg_t m;
	int result;

	m.m_data4= (char *)reg86p;

	result = ktaskcall(SYSTASK, SYS_INT86, &m);

	return(result);
}

