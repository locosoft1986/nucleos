/*
 *  Copyright (C) 2009  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
/*
 * (c) copyright 1987 by the Vrije Universiteit, Amsterdam, The Netherlands.
 * See the copyright notice in the ACK home directory, in the file "Copyright".
 */
#include	<stdlib.h>

static int tmp = -1;

div_t
div(register int numer, register int denom)
{
	div_t r;

	/* The assignment of tmp should not be optimized !! */
	if (tmp == -1) {
		tmp = (tmp / 2 == 0);
	}
	if (numer == 0) {
		r.quot = numer / denom;		/* might trap if denom == 0 */
		r.rem = numer % denom;
	} else if ( !tmp && ((numer < 0) != (denom < 0))) {
		r.quot = (numer / denom) + 1;
		r.rem = numer - (numer / denom + 1) * denom;
	} else {
		r.quot = numer / denom;
		r.rem = numer % denom;
	}
	return r;
}
