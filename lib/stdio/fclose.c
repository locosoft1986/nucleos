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
 * fclose.c - flush a stream and close the file
 */
#include	<stdio.h>
#include	<stdlib.h>
#include	"loc_incl.h"

int close(int d);

int
fclose(FILE *fp)
{
	register int i, retval = 0;

	for (i=0; i<FOPEN_MAX; i++)
		if (fp == __iotab[i]) {
			__iotab[i] = 0;
			break;
		}
	if (i >= FOPEN_MAX)
		return EOF;
	if (fflush(fp)) retval = EOF;
	if (close(fileno(fp))) retval = EOF;
	if ( io_testflag(fp,_IOMYBUF) && fp->_buf )
		free((void *)fp->_buf);
	if (fp != stdin && fp != stdout && fp != stderr)
		free((void *)fp);
	return retval;
}
