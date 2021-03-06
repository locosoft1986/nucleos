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
 * vsprintf - print formatted output without ellipsis on an array
 */
#include <nucleos/kernel.h>
#include <stdio.h>
#include <stdarg.h>
#include <nucleos/limits.h>
#include "loc_incl.h"

int
vsnprintf(char *s, size_t n, const char *format, va_list arg)
{
	int retval;
	FILE tmp_stream;

	tmp_stream._fd     = -1;
	tmp_stream._flags  = _IOWRITE + _IONBF + _IOWRITING;
	tmp_stream._buf    = (unsigned char *) s;
	tmp_stream._ptr    = (unsigned char *) s;
	tmp_stream._count  = n-1;

	retval = _doprnt(format, arg, &tmp_stream);
	tmp_stream._count  = 1;
	putc('\0',&tmp_stream);

	return retval;
}

int
vsprintf(char *s, const char *format, va_list arg)
{
	return vsnprintf(s, INT_MAX, format, arg);
}
