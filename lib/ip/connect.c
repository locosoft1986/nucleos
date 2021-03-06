/*
 *  Copyright (C) 2012  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
#include <nucleos/errno.h>
#include <nucleos/fcntl.h>
#include <stdio.h>
#include <nucleos/string.h>
#include <nucleos/ioctl.h>
#include <nucleos/socket.h>
#include <netinet/in.h>

#include <net/in.h>
#include <net/tcp.h>
#include <net/tcp_io.h>
#include <net/udp.h>
#include <net/udp_io.h>

#include <asm/ioctls.h>

#define DEBUG 0

static int _tcp_connect(int socket, const struct sockaddr *address,
	socklen_t address_len, nwio_tcpconf_t *tcpconfp);
static int _udp_connect(int socket, const struct sockaddr *address,
	socklen_t address_len, nwio_udpopt_t *udpoptp);

int connect(int socket, const struct sockaddr *address,
	socklen_t address_len)
{
	int r;
	nwio_tcpconf_t tcpconf;
	nwio_udpopt_t udpopt;

	r= ioctl(socket, NWIOGTCPCONF, &tcpconf);
	if (r != -1 || (errno != ENOTTY && errno != EBADIOCTL))
	{
		if (r == -1)
		{
			/* Bad file descriptor */
			return -1;
		}
		return _tcp_connect(socket, address, address_len, &tcpconf);
	}

	r= ioctl(socket, NWIOGUDPOPT, &udpopt);
	if (r != -1 || (errno != ENOTTY && errno != EBADIOCTL))
	{
		if (r == -1)
		{
			/* Bad file descriptor */
			return -1;
		}
		return _udp_connect(socket, address, address_len, &udpopt);
	}
#if DEBUG
	fprintf(stderr, "connect: not implemented for fd %d\n", socket);
#endif
	errno= ENOSYS;
	return -1;
}

static int _tcp_connect(int socket, const struct sockaddr *address,
	socklen_t address_len, nwio_tcpconf_t *tcpconfp)
{
	int r;
	struct sockaddr_in *sinp;
	nwio_tcpconf_t tcpconf;
	nwio_tcpcl_t tcpcl;

	if (address_len != sizeof(*sinp))
	{
		errno= EINVAL;
		return -1;
	}
	sinp= (struct sockaddr_in *)address;
	if (sinp->sin_family != AF_INET)
	{
		errno= EINVAL;
		return -1;
	}
	tcpconf.nwtc_flags= NWTC_SET_RA | NWTC_SET_RP;
	if ((tcpconfp->nwtc_flags & NWTC_LOCPORT_MASK) == NWTC_LP_UNSET)
		tcpconf.nwtc_flags |= NWTC_LP_SEL;
	tcpconf.nwtc_remaddr= sinp->sin_addr.s_addr;
	tcpconf.nwtc_remport= sinp->sin_port;

	if (ioctl(socket, NWIOSTCPCONF, &tcpconf) == -1)
        {
		/* Ignore EISCONN error. The NWIOTCPCONN ioctl will get the
		 * right error.
		 */
		if (errno != EISCONN)
			return -1;
	}

	tcpcl.nwtcl_flags= TCF_DEFAULT;

	r= fcntl(socket, F_GETFL);
	if (r == 1)
		return -1;
	if (r & O_NONBLOCK)
		tcpcl.nwtcl_flags |= TCF_ASYNCH;

	r= ioctl(socket, NWIOTCPCONN, &tcpcl);
	return r;
}

static int _udp_connect(int socket, const struct sockaddr *address,
	socklen_t address_len, nwio_udpopt_t *udpoptp)
{
	int r;
	struct sockaddr_in *sinp;
	nwio_udpopt_t udpopt;

	if (address == NULL)
	{
		/* Unset remote address */
		udpopt.nwuo_flags= NWUO_RP_ANY | NWUO_RA_ANY | NWUO_RWDATALL;

		r= ioctl(socket, NWIOSUDPOPT, &udpopt);
		return r;
	}

	if (address_len != sizeof(*sinp))
	{
		errno= EINVAL;
		return -1;
	}
	sinp= (struct sockaddr_in *)address;
	if (sinp->sin_family != AF_INET)
	{
		errno= EINVAL;
		return -1;
	}
	udpopt.nwuo_flags= NWUO_RP_SET | NWUO_RA_SET | NWUO_RWDATONLY;
	if ((udpoptp->nwuo_flags & NWUO_LOCPORT_MASK) == NWUO_LP_ANY)
		udpopt.nwuo_flags |= NWUO_LP_SEL;
	udpopt.nwuo_remaddr= sinp->sin_addr.s_addr;
	udpopt.nwuo_remport= sinp->sin_port;

	r= ioctl(socket, NWIOSUDPOPT, &udpopt);
	return r;
}
