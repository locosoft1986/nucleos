/*
 *  Copyright (C) 2012  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
/*	asynchio.h - Asynchronous I/O			Author: Kees J. Bot
 *								26 Jan 1995
 * This is just a fake async I/O library to be used for programs
 * written for Minix-vmd that must also run under standard Minix.
 * This limits the number of ugly #ifdefs somewhat.  The programs must
 * be restricted to performing just one service, of course.
 */
#ifndef _NUCLEOS_ASYNCHIO_H
#define _NUCLEOS_ASYNCHIO_H

#include <nucleos/time.h>

typedef struct {
	char	state;
	char	op;
	char	fd;
	char	req;
	void	*data;
	ssize_t	count;
	int	errno;
} asynchio_t;

#define ASYN_NONBLOCK	0x01

#define ASYN_INPROGRESS	EINPROGRESS

void asyn_init(asynchio_t *_asyn);
ssize_t asyn_read(asynchio_t *_asyn, int _fd, void *_buf, size_t _len);
ssize_t asyn_write(asynchio_t *_asyn, int _fd, const void *_buf, size_t _len);
int asyn_ioctl(asynchio_t *_asyn, int _fd, unsigned long _request, void *_data);
int asyn_wait(asynchio_t *_asyn, int _flags, struct timeval *to);
int asyn_synch(asynchio_t *_asyn, int _fd);
int asyn_close(asynchio_t *_asyn, int _fd);

#endif /* _NUCLEOS_ASYNCHIO_H */
