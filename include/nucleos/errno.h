/*
 *  Copyright (C) 2012  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
/* For kernel and user-kernel return negative error values. */

#ifndef _NUCLEOS_ERRNO_H
#define _NUCLEOS_ERRNO_H

#include <asm/errno.h>

#if defined(__KERNEL__) || defined(__UKERNEL__)
/* The following are not POSIX errors, but they can still happen. 
 * All of these are generated by the kernel and relate to message passing.
 */
#define EGENERIC		99  /* generic error */
#define ELOCKED			101  /* can't send message due to deadlock */
#define EBADCALL		102  /* illegal system call number */
#define EBADSRCDST		103  /* bad source or destination process */
#define ECALLDENIED		104  /* no permission for system call */
#define EDEADSRCDST		105  /* source or destination is not alive */
#define ENOTREADY		106  /* source or destination is not ready */
#define EBADREQUEST		107  /* destination cannot handle request */
#define ESRCDIED		108  /* source just died */
#define EDSTDIED		109  /* destination just died */
#define ETRAPDENIED		110  /* IPC trap not allowed */
#define EDONTREPLY		201  /* pseudo-code: don't send a reply */
#endif /* defined(__KERNEL__) || defined(__UKERNEL__) */

#endif /* _NUCLEOS_ERRNO_H */
